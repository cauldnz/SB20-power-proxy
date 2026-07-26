#!/usr/bin/env python3
"""Generate the Bridge-contract mirrors from the single source (ui-schema/bridge.json).

Mirrors the design/gen_tokens.py idiom (marker-block splice + --check CI mode), widened to the wire
contract. Emits, from ui-schema/bridge.json:
  - ui-schema/bridge-golden.json  : canonical {packet -> {values, bytes-hex}} vectors — the oracle every
                                    language asserts against (C++ Proto.h, the JS codec, the CIQ mirror).
  - web/bridge-codec.js           : the SPA's parse*/pack* for each packet (replaces the hand-coded
                                    DataView offsets in web/index.html — the highest drift risk).

    python code/scripts/gen_bridge.py            # (re)write the generated files
    python code/scripts/gen_bridge.py --check    # verify in sync (CI); exit 1 on drift

Design + rationale: design/ui-schema-design.md. The C++ Proto.h stays hand-written (its validation is
domain logic) and is LOCKED to this schema by asserting Proto.h pack/unpack == bridge-golden.json in a
firmware test. Node parity test asserts bridge-codec.js == bridge-golden.json.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "ui-schema" / "bridge.json"
GOLDEN = ROOT / "ui-schema" / "bridge-golden.json"
CODEC_JS = ROOT / "web" / "bridge-codec.js"
CPP_GOLDEN = ROOT / "firmware-nrf" / "test" / "test_bridge" / "bridge_golden_gen.h"

# ---- field model -----------------------------------------------------------------------------

INT_FMT = {"u8": "<B", "i8": "<b", "u16": "<H", "i16": "<h", "u32": "<I"}
INT_SIZE = {"u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4}


def field_size(f: dict) -> int:
    """Byte width of a fixed field (arrays handled separately)."""
    t = f["type"]
    n = f.get("repeat", 1)
    if t == "const":
        return 1 * n
    if t == "flags" or t == "enum":
        return 1 * n
    if t in INT_SIZE:
        return INT_SIZE[t] * n
    if t == "str":
        return f["len"] * n
    if t == "scaled":
        return INT_SIZE[f["raw"]] * n
    raise SystemExit(f"field_size: unknown fixed type {t!r}")


def slot_size(of_fields: list) -> int:
    return sum(field_size(f) for f in of_fields)


# ---- deterministic sample values (for the golden vectors) ------------------------------------
# Stable, distinctive per field so an offset/scale slip is obvious in the bytes.

def sample_scalar(name: str, t: str, f: dict):
    if t == "const" or "value" in f:
        return f["value"]  # const/reserved fields serialize their fixed value
    if t == "flags":
        # alternate bits on: first, third, ... so the flag byte is non-trivial
        return {b: (i % 2 == 0) for i, b in enumerate(f["bits"])}
    if t == "enum":
        return 1  # a mid enum value
    if f.get("bool"):
        return True
    if t == "str":
        return (name.upper()[:4] or "ABCD")
    if t == "scaled":
        # a value that survives *scale cleanly: 1.234 for x1000, or 15.0 for x10
        return 1.234 if f.get("scale") == 1000 else 15.0
    if t == "i8":
        return -7
    if t == "u8":
        return 42
    if t == "i16":
        return -321
    if t == "u16":
        return 4321
    if t == "u32":
        return 123456
    raise SystemExit(f"sample_scalar: unknown type {t!r}")


# ---- Python pack (computes the golden bytes) -------------------------------------------------

def pack_scalar(buf: bytearray, t: str, f: dict, val) -> None:
    if t == "const":
        buf.append(f["value"] & 0xFF)
    elif t == "flags":
        b = 0
        for i, name in enumerate(f["bits"]):
            if val.get(name):
                b |= (1 << i)
        buf.append(b)
    elif t == "enum":
        buf.append(int(val) & 0xFF)
    elif t == "str":
        s = str(val).encode("ascii")[: f["len"]]
        buf += s + b"\x00" * (f["len"] - len(s))
    elif t == "scaled":
        raw = round(val * f["scale"])
        buf += struct.pack(INT_FMT[f["raw"]], raw)
    elif t in INT_FMT:
        v = 1 if (f.get("bool") and val is True) else (0 if (f.get("bool") and val is False) else int(val))
        buf += struct.pack(INT_FMT[t], v)
    else:
        raise SystemExit(f"pack_scalar: unknown type {t!r}")


def pack_packet(pkt: dict, values: dict) -> bytes:
    buf = bytearray()
    for f in pkt["fields"]:
        if f["type"] == "array":
            items = values[f["name"]]
            buf.append(len(items))
            for item in items:
                for sub in f["of"]:
                    if "name" in sub:
                        pack_scalar(buf, sub["type"], sub, item[sub["name"]])
                    else:  # anonymous flags in a slot
                        pack_scalar(buf, sub["type"], sub, item["flags"])
            continue
        rep = f.get("repeat", 1)
        if rep > 1:
            vals = values[f["name"]]
            for i in range(rep):
                pack_scalar(buf, f["type"], f, vals[i])
            continue
        if "name" in f:
            pack_scalar(buf, f["type"], f, values[f["name"]])
        else:  # anonymous flags at packet level
            pack_scalar(buf, f["type"], f, values["flags"])
    return bytes(buf)


def sample_packet(pkt: dict) -> dict:
    """A deterministic value dict for a packet (used for the golden vector)."""
    out = {}
    for f in pkt["fields"]:
        if f["type"] == "array":
            n = 2  # two representative rows
            rows = []
            for _ in range(n):
                row = {}
                for sub in f["of"]:
                    key = sub.get("name", "flags")
                    row[key] = sample_scalar(key, sub["type"], sub)
                rows.append(row)
            out[f["name"]] = rows
            continue
        rep = f.get("repeat", 1)
        if rep > 1:
            out[f["name"]] = [sample_scalar(f["name"], f["type"], f) for _ in range(rep)]
            continue
        key = f.get("name", "flags")
        out[key] = sample_scalar(key, f["type"], f)
    return out


def check_fixed_len(name: str, pkt: dict) -> None:
    """Guard: a fixed packet's declared `len` must equal the sum of its field sizes — else the JS
    (which allocates `len`) and the byte-appending golden disagree on trailing bytes (caught WkState's
    2-vs-4-byte reserved tail, 2026-07-11)."""
    if pkt.get("variable") or "len" not in pkt:
        return
    total = 0
    for f in pkt["fields"]:
        if f["type"] == "array":
            raise SystemExit(f"{name}: array field in a fixed-len packet")
        total += field_size(f)
    if total != pkt["len"]:
        raise SystemExit(f"{name}: field sizes sum to {total} but declared len is {pkt['len']} "
                         f"— fix the schema so they match Proto.h")


def build_golden(schema: dict) -> dict:
    out = {"_doc": "GENERATED by gen_bridge.py — do not edit. Canonical (values -> bytes) vectors; the "
                   "oracle the C++ Proto.h test + the JS Node test both assert against.",
           "proto_ver": schema["proto_ver"], "packets": {}}
    for name, pkt in schema["packets"].items():
        check_fixed_len(name, pkt)
        vals = sample_packet(pkt)
        by = pack_packet(pkt, vals)
        if not pkt.get("variable") and "len" in pkt and len(by) != pkt["len"]:
            raise SystemExit(f"{name}: packed {len(by)} bytes != declared len {pkt['len']}")
        out["packets"][name] = {"values": vals, "bytes": by.hex()}
    return out


# ---- JS codec emitter ------------------------------------------------------------------------

def js_reader(f: dict, off_expr: str) -> str:
    t = f["type"]
    if t in ("u8", "enum"):
        return f"dv.getUint8({off_expr})"
    if t == "i8":
        return f"dv.getInt8({off_expr})"
    if t == "u16":
        return f"dv.getUint16({off_expr}, true)"
    if t == "i16":
        return f"dv.getInt16({off_expr}, true)"
    if t == "u32":
        return f"dv.getUint32({off_expr}, true)"
    if t == "scaled":
        raw = "getUint16" if f["raw"] == "u16" else "getInt16"
        return f"dv.{raw}({off_expr}, true) / {f['scale']}"
    raise SystemExit(f"js_reader: {t}")


def emit_js(schema: dict) -> str:
    L = []
    L.append("// GENERATED by code/scripts/gen_bridge.py from ui-schema/bridge.json — DO NOT EDIT.")
    L.append("// The Bridge GATT parse/pack the SPA imports; asserted against ui-schema/bridge-golden.json")
    L.append("// by the Node parity test. Edit the schema + rerun the generator, never this file.")
    L.append(f"export const PROTO_VER = {schema['proto_ver']};")
    L.append("")
    L.append("function readStr(dv, off, len) {")
    L.append("  let s = ''; for (let i = 0; i < len; i++) { const c = dv.getUint8(off + i); if (!c) break; s += String.fromCharCode(c); } return s;")
    L.append("}")
    L.append("function writeStr(bytes, off, len, s) {")
    L.append("  for (let i = 0; i < len; i++) bytes[off + i] = i < s.length ? s.charCodeAt(i) & 0xff : 0;")
    L.append("}")
    L.append("")
    for name, pkt in schema["packets"].items():
        L.append(emit_js_packet(name, pkt))
        L.append("")
    return "\n".join(L)


def emit_js_packet(name: str, pkt: dict) -> str:
    """parse<Name>(DataView)->obj  and  pack<Name>(obj)->Uint8Array."""
    P, U = [], []
    # ---- parse ----
    P.append(f"export function parse{name}(dv) {{")
    P.append("  const o = {};")
    off = 0
    var = pkt.get("variable")
    for f in pkt["fields"]:
        t = f["type"]
        if t == "const":
            off += 1
            continue
        if t == "array":
            slot = f.get("slot_len") or slot_size(f["of"])
            P.append(f"  {{ const n = dv.getUint8({off}); const arr = []; let p = {off + 1};")
            P.append("    for (let i = 0; i < n; i++) { const it = {};")
            so = 0
            for sub in f["of"]:
                key = sub.get("name", "flags")
                if sub["type"] == "flags":
                    P.append(f"      {{ const b = dv.getUint8(p + {so}); it.{key} = {{" +
                             ", ".join(f"{bn}: !!(b & {1 << i})" for i, bn in enumerate(sub['bits'])) + "}; }")
                elif sub["type"] == "str":
                    P.append(f"      it.{key} = readStr(dv, p + {so}, {sub['len']});")
                else:
                    P.append(f"      it.{key} = {js_reader(sub, f'p + {so}')};")
                so += field_size(sub)
            P.append(f"      arr.push(it); p += {slot}; }}")
            P.append(f"    o.{f['name']} = arr; }}")
            off = None  # variable tail
            continue
        rep = f.get("repeat", 1)
        if rep > 1:
            P.append(f"  o.{f['name']} = [" + ", ".join(js_reader(f, str(off + i)) for i in range(rep)) + "];")
            off += field_size(f)
            continue
        key = f.get("name", "flags")
        if t == "flags":
            P.append(f"  {{ const b = dv.getUint8({off}); o.{key} = {{" +
                     ", ".join(f"{bn}: !!(b & {1 << i})" for i, bn in enumerate(f['bits'])) + "}; }")
        elif t == "str":
            P.append(f"  o.{key} = readStr(dv, {off}, {f['len']});")
        elif f.get("bool"):
            P.append(f"  o.{key} = !!dv.getUint8({off});")
        else:
            P.append(f"  o.{key} = {js_reader(f, str(off))};")
        off += field_size(f)
    P.append("  return o;")
    P.append("}")

    # ---- pack ----
    total = None if var else pkt.get("len")
    U.append(f"export function pack{name}(o) {{")
    if total is not None:
        U.append(f"  const bytes = new Uint8Array({total});")
        U.append("  const dv = new DataView(bytes.buffer);")
    off = 0
    writes = []
    for f in pkt["fields"]:
        t = f["type"]
        if t == "const":
            writes.append(f"  bytes[{off}] = {f['value']};")
            off += 1
            continue
        if t == "array":
            # variable pack: build dynamically
            slot = f.get("slot_len") or slot_size(f["of"])
            U2 = []
            U2.append(f"  const items = o.{f['name']} || [];")
            U2.append(f"  const bytes = new Uint8Array({off + 1} + items.length * {slot});")
            U2.append("  const dv = new DataView(bytes.buffer);")
            for w in writes:
                U2.append(w)
            U2.append(f"  bytes[{off}] = items.length;")
            U2.append(f"  let p = {off + 1};")
            U2.append("  for (const it of items) {")
            so = 0
            for sub in f["of"]:
                key = sub.get("name", "flags")
                U2.append("    " + js_writer(sub, f"p + {so}", f"it.{key}"))
                so += field_size(sub)
            U2.append(f"    p += {slot}; }}")
            U2.append("  return bytes;")
            U2.append("}")
            U = U[:1] + U2  # replace fixed-buffer preamble
            return "\n".join(P) + "\n\n" + "\n".join(U)
        rep = f.get("repeat", 1)
        if rep > 1:
            for i in range(rep):
                writes.append("  " + js_writer(f, str(off + i), f"(o.{f['name']}||[])[{i}]"))
            off += field_size(f)
            continue
        key = f.get("name", "flags")
        writes.append("  " + js_writer(f, str(off), f"o.{key}"))
        off += field_size(f)
    U += writes
    U.append("  return bytes;")
    U.append("}")
    return "\n".join(P) + "\n\n" + "\n".join(U)


def js_writer(f: dict, off_expr: str, val_expr: str) -> str:
    t = f["type"]
    if t == "flags":
        terms = " | ".join(f"(({val_expr} && {val_expr}.{bn}) ? {1 << i} : 0)" for i, bn in enumerate(f["bits"]))
        return f"bytes[{off_expr}] = {terms};"
    if t == "str":
        return f"writeStr(bytes, {off_expr}, {f['len']}, {val_expr} || '');"
    if t == "scaled":
        setter = "setUint16" if f["raw"] == "u16" else "setInt16"
        return f"dv.{setter}({off_expr}, Math.round(({val_expr} || 0) * {f['scale']}), true);"
    if f.get("bool"):
        return f"bytes[{off_expr}] = {val_expr} ? 1 : 0;"
    if t in ("u8", "enum"):
        return f"bytes[{off_expr}] = ({val_expr} || 0) & 0xff;"
    if t == "i8":
        return f"dv.setInt8({off_expr}, {val_expr} || 0);"
    if t == "u16":
        return f"dv.setUint16({off_expr}, ({val_expr} || 0) & 0xffff, true);"
    if t == "i16":
        return f"dv.setInt16({off_expr}, {val_expr} || 0, true);"
    if t == "u32":
        return f"dv.setUint32({off_expr}, ({val_expr} >>> 0), true);"
    raise SystemExit(f"js_writer: {t}")


# ---- main ------------------------------------------------------------------------------------

def emit_cpp_golden(golden: dict) -> str:
    L = ["#pragma once",
         "// GENERATED by code/scripts/gen_bridge.py from ui-schema/bridge.json — DO NOT EDIT.",
         "// Golden byte vectors (hex) for the Bridge packets; test_bridge builds each struct with the",
         "// schema's sample values and asserts Proto.h pack() == these bytes, locking Proto.h to the",
         "// schema (so a Proto.h offset change without a schema update fails CI).",
         "namespace bridge_golden {"]
    for name, g in golden["packets"].items():
        L.append(f'inline constexpr const char* {name} = "{g["bytes"]}";')
    L.append("}  // namespace bridge_golden")
    L.append("")
    return "\n".join(L)


def generate(schema: dict) -> dict[Path, str]:
    golden = build_golden(schema)
    return {
        GOLDEN: json.dumps(golden, indent=2) + "\n",
        CODEC_JS: emit_js(schema),
        CPP_GOLDEN: emit_cpp_golden(golden),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="verify sync only; exit 1 on drift")
    args = ap.parse_args()

    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    outputs = generate(schema)

    drifted = []
    for path, content in outputs.items():
        cur = path.read_text(encoding="utf-8") if path.exists() else None
        if cur != content:
            drifted.append(path.relative_to(ROOT).as_posix())
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")

    if args.check:
        if drifted:
            print("Bridge mirrors out of sync with ui-schema/bridge.json:", ", ".join(drifted))
            print("Run: python code/scripts/gen_bridge.py")
            return 1
        print(f"Bridge mirrors in sync ({len(outputs)} files).")
        return 0
    print("Wrote", len(outputs), "files" + (f" (updated: {', '.join(drifted)})" if drifted else " (already in sync)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
