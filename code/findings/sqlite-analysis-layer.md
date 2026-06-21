# SQLite analysis layer — a rebuildable index over the JSONL captures

**Status: prototype built + tested (2026-06-21).** Importer, schema, and the reconcile
helper live in [`src/sb20proxy/analysis/jsonl_sqlite.py`](../src/sb20proxy/analysis/jsonl_sqlite.py),
the build/query CLI in [`scripts/13_build_sqlite.py`](../scripts/13_build_sqlite.py), and hermetic
tests in [`tests/test_analysis_sqlite.py`](../tests/test_analysis_sqlite.py). Raised by the owner
during bike session 4: a SQLite store "may make it easier to work with" than raw log files for
desk-side reconciliation. This note is the design; the code is the prototype.

## Why, and the one hard rule

The committed JSONL in [`findings/captures/`](captures/) is the **canonical, lossless record and is
never edited** — every analysis derives from it. Raw log lines are awkward to slice, though: "show me
every second where the SB20's FTMS power and the Assioma's ANT+ power disagree by >10 W" is a painful
hand-rolled pass (it's literally what [`08_analyze_grid.py`](../scripts/08_analyze_grid.py) does by
hand). SQLite turns those into a `JOIN`.

So the layer is a **derived index, not a new source of truth**:

- **JSONL stays canonical.** The SQLite DB is a *cache* — delete it and rebuild from the JSONL at any
  time. It is `.gitignore`d (`*.sqlite`); never committed. The capture format is **unchanged**.
- **Lossless.** Every JSONL line is stored verbatim in `record.raw_json`, so even record kinds we don't
  flatten survive, and any flattened table can be re-derived.
- **Idempotent.** A capture is keyed by `filename`; every row by `(capture_id, rec_index)` — its 0-based
  line number. Re-importing an unchanged file is a no-op (content-hashed); a changed file (captures are
  immutable, so this only happens on a re-cut or a torn write) is replaced wholesale.
- **Decode is reused, not reinvented.** Indoor Bike Data and control-point bytes are decoded with the
  already-validated codec in [`ble/ftms.py`](../src/sb20proxy/ble/ftms.py) — the twin of the firmware
  `Ftms.h`. No second decoder to keep in sync.

## Schema

One row per capture, one row per JSONL line, plus a flattened/decoded table per common kind. Everything
else still lands losslessly in `record`.

| Table | Source kind(s) | Key decoded columns |
|---|---|---|
| `capture` | `session_start` + file | `filename`, `sha256`, `protocol` (ble/ant/ant+multi), `n_records` |
| `record` | **every line** | `kind`, `iso_time`, `monotonic_s`, `raw_json` (verbatim) |
| `ble_notification` | `ble_notification` (non-CP) | `char`, `raw_hex`, `flags`, `power_w`, `cadence_rpm`, `speed_kmh`, `avg_power_w`, `total_distance_m` |
| `ble_control_point` | `ble_cp_write`, `ble_cp_indication`, CP-char `ble_notification` | `direction` (write/response/indication), `opcode`, `result`, `result_name`, `target_power_w` |
| `ble_advertisement` | `ble_advertisement` | `address`, `name`, `rssi`, `service_uuids` (JSON), `manufacturer_data` (JSON) |
| `ant_broadcast` | `broadcast`, `acknowledged` | `source`, `page`, `power_w`, `cadence_rpm`, `accumulated_power/torque`, `ext_device_number` |

Indoor Bike Data is decoded per [`ftms-protocol.md`](ftms-protocol.md) (flags `u16`, then present fields
in ascending bit order; the bit-0 *More-Data* speed-present inversion is handled by the codec). A
notification on the **control-point characteristic** is routed to `ble_control_point` as a response, not
to `ble_notification`, so the CP table holds both directions of the erg handshake.

### The reconciliation view

`power_sample` unifies the two power sources into one `(stream, power_w, cadence_rpm)` shape:

```sql
CREATE VIEW power_sample AS
  SELECT capture_id, monotonic_s, iso_time, COALESCE(source,'ant') AS stream,
         power_w, CAST(cadence_rpm AS REAL) AS cadence_rpm
    FROM ant_broadcast WHERE power_w IS NOT NULL
  UNION ALL
  SELECT capture_id, monotonic_s, iso_time, 'ftms' AS stream,
         power_w, cadence_rpm
    FROM ble_notification WHERE char='indoor_bike_data' AND power_w IS NOT NULL;
```

`stream` is the meter identity — the ANT channel label (`stages`, `assioma`, `bike_fec`) or `ftms` for the
SB20's Indoor Bike Data. `reconcile()` buckets two streams on a time basis and reports the per-bucket
delta/ratio, carrying **both meters' cadence** (the SB20↔Assioma offset is cadence-dependent, so a fit
must bin by cadence).

## The two use cases

**1 — Stages crank vs Favero Assioma (§D calibration grid).** Both meters are ANT+ in one
`07_capture_multi.py` file with `source` labels and one clock → `basis="mono"` (per-second monotonic
buckets). This is exactly the pairing `08_analyze_grid.py` does, as SQL. **Validated on real data:** on
[`QUICK-multi-20260615-064037.jsonl`](captures/QUICK-multi-20260615-064037.jsonl), `reconcile(bike_fec,
stages)` gives **147 paired seconds, median ratio 0.996** — reproducing the committed #7 pass-through
result (~0.997) the test asserts against.

```bash
python scripts/13_build_sqlite.py --reconcile \
    --capture CAL-grid-<ts>.jsonl --stream-a stages --stream-b assioma
```

**2 — SB20 FTMS power vs an independent ANT+ Assioma feed** (the PLAYBOOK pre-flight "independent
reference-meter feed"). The SB20's FTMS power is BLE; the reference Assioma is a separate ANT+ radio →
two captures, aligned on **wall-clock** `iso_time` (`basis="iso"`). The mechanism is the same
`reconcile()`; only the time basis differs.

```bash
python scripts/13_build_sqlite.py --reconcile --basis iso \
    --capture-a G-sb20-ftms-erg-<ts>.jsonl --stream-a ftms \
    --capture-b ASSIOMA-antfeed-<ts>.jsonl --stream-b assioma
```

> Use case 2's *paired* capture doesn't exist yet (it's what the reference-meter pre-flight item adds),
> so the cross-capture `iso` path is built and unit-tested for mechanism but not yet validated on a real
> simultaneous pair. Real-data-first: it lights up the first session that logs both feeds. Note `iso_time`
> is whole-second resolution in current captures → 1 s is the natural bucket.

## Schema-variance found in the real captures (the reason to build from data, not docs)

Reading the actual files surfaced shape differences a docs-first parser would have missed — each is
handled and pinned by a test:

- **`ble_notification` has two shapes.** The hw-loop capture logs IBD flat with decoded fields
  (`power_w`, `cadence_rpm` inline); the erg capture nests `{"data": {"raw_hex", "flags_hex"}}` with **no
  decoded power**. We always decode from `raw_hex` (present in both) so the erg capture isn't left blank.
- **`ble_cp_write` has two shapes.** FTMS: `{note, raw_hex}` (e.g. `note="set_target_power=150W"`,
  `raw_hex="059600"`). CPS: `{op, opcode}` with **no raw_hex** (e.g. `op="request-crank-length"`).
- **CP responses are sometimes pre-decoded, sometimes not.** The erg/CPS captures carry
  `data.request_opcode/result/result_name`; the hw-loop capture is flat `raw_hex` only. We prefer the
  capture's own decode and fall back to the FTMS codec for the flat case.
- **The char name drifts** (`control_point` vs `fitness_machine_control_point`) — normalised on import.
- **Decoded ANT power can carry artefacts** (a stray `13567 W` frame in the QUICK multi-capture). The
  `source` channel label is the reliable stream key; `reconcile()`'s `min_power/max_power` gate drops
  artefacts and idle samples. `raw_hex` remains authoritative.

## Build & use

```bash
# build / refresh code/captures.sqlite from every committed capture (idempotent)
python scripts/13_build_sqlite.py
python scripts/13_build_sqlite.py --rebuild        # from scratch
```

Module API: `connect(db)`, `import_capture(conn, path)` / `import_dir(conn, dir)`, and
`reconcile(conn, stream_a, stream_b, capture=..., basis="mono"|"iso", min_power=, min_cadence=)` →
`list[ReconRow]` (`.delta`, `.ratio`), with `reconcile_summary()` for the aggregate. The DB is stdlib
`sqlite3` — **no new dependency**.

## Annotations — post-processing write-back (designed; next step)

The natural follow-on: during desk-side post-processing, author **annotations** — segment labels
(`cell_200W_60rpm`, `coast notch`, `erg hold target=150 W`), point flags (`suspect — decode artefact`),
and derived verdicts (`Δ = +25 W @ 60 rpm`) — then `JOIN` them against `power_sample`. This is the
machine-queryable form of what `decisions.md` and the session docs already record in prose.

**Annotations are canonical committed *text*; the DB stays derived.** They live in a sidecar —
`findings/annotations/<capture>.jsonl`, one object per line — keyed to **immutable capture coordinates**
(the capture filename + a `rec_index` range or a `monotonic_s` range), so they bind to bytes that never
change. The importer materialises them into an `annotation` table on build; authoring can be DB-first for
convenience (`annotate()` upserts the table *and* appends the sidecar). Because the canonical copy is the
committed sidecar, **`--rebuild` re-materialises them and nothing is lost** — the DB stays a disposable
cache. (The `annotation` table is keyed by filename, not FK-cascaded to `capture_id`, so re-importing a
capture doesn't drop its annotations.)

```sql
-- median Stages/Assioma ratio inside the labelled 200W@60rpm cell
SELECT AVG(...) FROM power_sample ps
  JOIN annotation a ON a.capture = ps.capture
 WHERE a.label = 'cell_200W_60rpm' AND ps.monotonic_s BETWEEN a.t_start_s AND a.t_end_s;
```

A post-processing pass can also **derive** annotations automatically — erg holds (captures already log
`ble_erg_hold`), steady blocks, coast notches — so the labels that drive a calibration fit aren't placed
by hand.

### Why the DB itself is derived (gitignored), not committed

Worth stating plainly, since "just commit the DB" is tempting. The instinct to *commit the data* is
right — so we commit it **as text** (the JSONL captures, and now the annotation sidecars). We don't commit
the **binary `.sqlite`**, because:

- **It's a pure function of committed text** (JSONL + annotation sidecars), rebuilt in seconds by
  `13_build_sqlite.py`. Committing it duplicates MBs that can silently drift from the source (add a
  capture, forget to rebuild → a stale DB is committed).
- **It's binary and non-deterministic** (a wall-clock `imported_at`, SQLite page layout), so a PR shows
  only `Binary files differ` — the reviewer (here, the original session) can't see what changed — and two
  concurrent sessions rebuilding it get **unresolvable binary merge conflicts**. That's the exact
  multi-session hazard this repo is built to avoid.

Text-canonical keeps every interpretation diffable, mergeable, and reviewable. **Reversible if we ever
disagree:** drop the wall-clock column (make the build byte-deterministic), remove one `.gitignore` line,
and commit a snapshot — but for PR review across concurrent sessions, committed *text* + a derived DB is
the safer default. Flagged for the original session to confirm.

## What's validated vs pending

- ✅ Import (idempotent, lossless, protocol inference), IBD decode (both shapes), CP decode (write +
  response, both shapes), advertisements, ANT broadcast, and **use-case-1 reconciliation against the
  documented #7 result** — all hermetic, against the real committed captures.
- ⏳ Use-case-2 cross-capture (`iso`) reconciliation — mechanism tested; awaits the first simultaneous
  SB20-FTMS + ANT+-Assioma capture to validate end-to-end.
- ↪ Agreed next step (design above, owner-approved 2026-06-21): the **annotation** layer — committed
  text sidecar + materialised table + auto-annotation pass.
- ↪ Other follow-ups (out of scope here): a cadence-binned ratio surface view (subsume
  `08_analyze_grid.py`), and feeding `reconcile()` output into `09_fit_calibration.py`.
