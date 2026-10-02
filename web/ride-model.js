// ride-model.js — the pure half of the shared SPA's ride view (#351). No DOM, no fetch, no timers:
// index.html feeds these from its transports and draws what they return, and
// web/test/ride-model.test.mjs runs them under Node. Served next to index.html on GitHub Pages
// (an ES module import) and INLINED into the board's copy by web/gen_spa_header.py, like
// bridge-codec.js — so the ESP32 still serves exactly one file at GET /app.

// Seconds -> "m:ss" (negative and fractional inputs are clamped/rounded, never "-0:-3").
export function mmss(s) {
  const t = Math.max(0, Math.round(+s || 0));
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, "0")}`;
}

// The erg leg as words (F17). `ergConfigured` is false only when the transport KNOWS no trainer is
// set (HTTP reads /status `trainer`); BLE leaves it undefined, so it reads as configured.
export function ergText(w) {
  if (!w) return "–";
  if (w.ergConfigured === false) return "no trainer";
  if (w.ergControlled) return "controlling";
  if (w.ergConnected) return "connected";
  return "searching…";
}

// What the ride hero and ride mode show for the workout (F16). Inactive unless a loaded workout is
// running (a paused one still shows, flagged). `seg` prefers the segment's label (HTTP carries it as
// seg_label); the nRF's Wk has no label, so it falls back to "segment i/n".
export function rideWorkout(w) {
  if (!w || !w.loaded || !w.running) return { active: false };
  const bias = w.bias || 0;
  return {
    active: true,
    paused: !!w.paused,
    target: w.target > 0 ? w.target : null,
    bias,
    seg: w.segLabel ? w.segLabel : w.nSeg ? `segment ${w.segIndex + 1}/${w.nSeg}` : "",
    left: mmss(w.segRemain),
    erg: ergText(w),
  };
}

// Power history for the ride chart (F14). Fixed capacity, at most one sample per `minIntervalMs`
// so a 2 Hz BLE stream and a 1 Hz HTTP poll both give a 90-second window (the legacy `/` page's 90
// samples). Each sample keeps the workout target alongside, so the chart can draw both lines.
export class PowerHistory {
  constructor(capacity = 90, minIntervalMs = 1000) {
    this.capacity = capacity;
    this.minIntervalMs = minIntervalMs;
    this.watts = [];
    this.targets = [];
    this.lastMs = -Infinity;
  }
  // Returns true if the sample was kept (the caller redraws only then).
  push(watts, target, nowMs) {
    if (nowMs - this.lastMs < this.minIntervalMs) return false;
    this.lastMs = nowMs;
    this.watts.push(watts >= 0 ? watts : null);
    this.targets.push(target > 0 ? target : null);
    if (this.watts.length > this.capacity) {
      this.watts.shift();
      this.targets.shift();
    }
    return true;
  }
  get length() { return this.watts.length; }
  // The y-axis top: the largest value seen (watts or target), rounded up to 50 W, never below 100.
  top() {
    let m = 100;
    for (const v of this.watts) if (v !== null && v > m) m = v;
    for (const v of this.targets) if (v !== null && v > m) m = v;
    return Math.ceil(m / 50) * 50;
  }
  // Canvas points for one series, newest at the right edge; null entries are gaps (no reading).
  points(series, width, height, pad = 3) {
    const vals = series === "target" ? this.targets : this.watts;
    const top = this.top(), n = vals.length, span = Math.max(1, this.capacity - 1);
    return vals.map((v, i) => v === null ? null : [
      width * (this.capacity - n + i) / span,
      height - pad - (v / top) * (height - 2 * pad),
    ]);
  }
}

// HTTP link health (F18). /status is the heartbeat: every poll reports ok(uptimeS) or fail().
//   * `failLimit` consecutive failures while up -> {event:"down"}: the board is gone (rebooting,
//     out of range) — the view revokes the green dot and says "reconnecting".
//   * the next success -> {event:"recovered"}; the very first -> {event:"up"}.
//   * uptime going BACKWARDS while up -> {event:"rebooted"}: the board restarted between two polls
//     (a fast reboot can fit between them); `rebooted` is also set on a recovery that crossed one.
// A single failed poll is ignored: one dropped packet on a busy 2.4 GHz band is not an outage.
export class LinkMonitor {
  constructor(failLimit = 2) {
    this.failLimit = failLimit;
    this.state = "connecting";  // "connecting" | "up" | "reconnecting"
    this.fails = 0;
    this.uptimeS = null;
  }
  ok(uptimeS) {
    const rebooted = this.uptimeS !== null && uptimeS + 1 < this.uptimeS;
    this.uptimeS = uptimeS;
    this.fails = 0;
    if (this.state !== "up") {
      const first = this.state === "connecting";
      this.state = "up";
      return { event: first ? "up" : "recovered", rebooted };
    }
    return rebooted ? { event: "rebooted", rebooted: true } : null;
  }
  fail() {
    this.fails++;
    if (this.state === "up" && this.fails >= this.failLimit) {
      this.state = "reconnecting";
      return { event: "down" };
    }
    return null;
  }
}
