// Node parity test: every packet's generated JS codec (web/bridge-codec.js) must round-trip the
// canonical byte vectors in ui-schema/bridge-golden.json (which the C++ Proto.h test also asserts).
// This is what stops web/index.html's Bridge codec from drifting off Proto.h. Run:
//   node web/test/bridge-codec.test.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import * as codec from "../bridge-codec.js";

const here = dirname(fileURLToPath(import.meta.url));
const golden = JSON.parse(
  readFileSync(join(here, "..", "..", "ui-schema", "bridge-golden.json"), "utf8"),
);

const toHex = (u8) => Buffer.from(u8).toString("hex");

let fails = 0;
for (const [name, g] of Object.entries(golden.packets)) {
  const pack = codec["pack" + name];
  const parse = codec["parse" + name];
  if (!pack || !parse) {
    console.error(`MISSING codec for ${name}`);
    fails++;
    continue;
  }
  // 1) pack(values) reproduces the golden bytes
  const packed = toHex(pack(g.values));
  if (packed !== g.bytes) {
    console.error(`FAIL ${name} pack:\n  got  ${packed}\n  want ${g.bytes}`);
    fails++;
    continue;
  }
  // 2) parse(bytes) then re-pack reproduces the bytes (proves parse is the inverse)
  const bytes = Uint8Array.from(Buffer.from(g.bytes, "hex"));
  const repacked = toHex(pack(parse(new DataView(bytes.buffer))));
  if (repacked !== g.bytes) {
    console.error(`FAIL ${name} parse->pack:\n  got  ${repacked}\n  want ${g.bytes}`);
    fails++;
    continue;
  }
  console.log(`ok ${name}`);
}
if (fails) {
  console.error(`\n${fails} packet(s) failed against the golden vectors`);
  process.exit(1);
}
console.log("\nbridge-codec parity: all packets match ui-schema/bridge-golden.json");
