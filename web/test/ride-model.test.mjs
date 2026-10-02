// Node tests for web/ride-model.js — the pure half of the SPA's ride view (#351): the HTTP link
// monitor behind "reconnecting" (F18), the hero/ride-mode workout line (F16/F17) and the power
// history the chart draws (F14). Run:
//   node web/test/ride-model.test.mjs
import assert from "node:assert/strict";
import { mmss, ergText, rideWorkout, PowerHistory, LinkMonitor } from "../ride-model.js";

let n = 0;
const test = (name, fn) => { fn(); n++; console.log(`ok   ${name}`); };

test("mmss formats and clamps", () => {
  assert.equal(mmss(0), "0:00");
  assert.equal(mmss(192), "3:12");
  assert.equal(mmss(-5), "0:00");
  assert.equal(mmss(59.6), "1:00");
  assert.equal(mmss(undefined), "0:00");
});

test("ergText walks the erg states", () => {
  assert.equal(ergText({ ergConfigured: false, ergConnected: true }), "no trainer");
  assert.equal(ergText({ ergConnected: true, ergControlled: true }), "controlling");
  assert.equal(ergText({ ergConnected: true }), "connected");
  assert.equal(ergText({}), "searching…");
  assert.equal(ergText(null), "–");
});

// The /workout/state the board serves 700 s into the 4x8 preset (WorkoutEngine.h fields, normalized
// by HttpTransport) — see the native test_workout_state_appends_erg_fields_to_the_engine_json.
const running = { loaded: true, running: true, paused: false, ergConnected: true, ergControlled: true,
  target: 248, segIndex: 1, nSeg: 9, segRemain: 380, elapsed: 700, bias: 10, segLabel: "Interval 1" };

test("rideWorkout shows target, segment label and time left while running", () => {
  const r = rideWorkout(running);
  assert.equal(r.active, true);
  assert.equal(r.target, 248);
  assert.equal(r.bias, 10);
  assert.equal(r.seg, "Interval 1");
  assert.equal(r.left, "6:20");
  assert.equal(r.erg, "controlling");
});

test("rideWorkout falls back to segment i/n without a label (the nRF Wk has none)", () => {
  assert.equal(rideWorkout({ ...running, segLabel: "" }).seg, "segment 2/9");
});

test("rideWorkout is inactive unless loaded and running; a paused run still shows", () => {
  assert.equal(rideWorkout(null).active, false);
  assert.equal(rideWorkout({ ...running, running: false }).active, false);
  assert.equal(rideWorkout({ ...running, loaded: false }).active, false);
  assert.equal(rideWorkout({ ...running, paused: true }).paused, true);
  assert.equal(rideWorkout({ ...running, target: -1 }).target, null);  // a free segment: no target
});

test("PowerHistory keeps at most one sample per interval and a fixed window", () => {
  const h = new PowerHistory(3, 1000);
  assert.equal(h.push(100, 0, 0), true);
  assert.equal(h.push(999, 0, 500), false);  // 2 Hz BLE: the second sample in a second is dropped
  h.push(200, 250, 1000); h.push(300, 250, 2000); h.push(400, 250, 3000);
  assert.deepEqual(h.watts, [200, 300, 400]);
  assert.deepEqual(h.targets, [250, 250, 250]);
  assert.equal(h.top(), 400);
});

test("PowerHistory points: newest at the right edge, gaps for missing readings", () => {
  const h = new PowerHistory(4, 0);
  h.push(-1, 0, 0); h.push(100, 0, 1);
  const pts = h.points("watts", 300, 100, 0);
  assert.equal(pts[0], null);                 // -1 = no reading -> a gap, not a zero
  assert.deepEqual(pts[1], [300, 0]);         // 100 W of a 100 W axis, at the right edge
  assert.deepEqual(h.points("target", 300, 100, 0), [null, null]);
});

test("LinkMonitor: first success is 'up'; one dropped poll is not an outage", () => {
  const m = new LinkMonitor(2);
  assert.deepEqual(m.ok(10), { event: "up", rebooted: false });
  assert.equal(m.ok(11), null);
  assert.equal(m.fail(), null);
  assert.equal(m.state, "up");
  assert.equal(m.ok(13), null);
});

test("LinkMonitor: a board reboot reads down -> recovered(rebooted)", () => {
  const m = new LinkMonitor(2);
  m.ok(400);
  assert.equal(m.fail(), null);
  assert.deepEqual(m.fail(), { event: "down" });
  assert.equal(m.state, "reconnecting");
  assert.equal(m.fail(), null);               // already down: no repeat event
  assert.deepEqual(m.ok(3), { event: "recovered", rebooted: true });
  assert.equal(m.state, "up");
});

test("LinkMonitor: uptime going backwards with no failed poll is still a reboot", () => {
  const m = new LinkMonitor(2);
  m.ok(400); m.fail();                        // the reboot fit between polls
  assert.deepEqual(m.ok(2), { event: "rebooted", rebooted: true });
  assert.equal(m.ok(3), null);
});

test("LinkMonitor: an outage without a reboot recovers with rebooted=false", () => {
  const m = new LinkMonitor(2);
  m.ok(400); m.fail(); m.fail();
  assert.deepEqual(m.ok(406), { event: "recovered", rebooted: false });
});

console.log(`\n${n} ride-model tests passed`);
