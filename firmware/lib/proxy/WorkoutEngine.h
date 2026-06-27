#pragma once
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "Status.h"  // jsonEscape (shared JSON string escaper)

namespace sb20proxy {

// The on-device WORKOUT engine — a pure, host-tested port of the Python ride director
// (code/src/sb20proxy/ride/director.py). It holds a structured workout (segments), parses the
// canonical JSON the desk tooling / web UI uploads, and steps it deterministically on a clock so the
// device can drive the SB20's erg loop and the Ride/Workout screen can render the profile + live
// cursor. The radio (the FTMS Set-Target-Power write) is the seam; everything here runs with no
// hardware. Format is 1:1 with the Python Segment, so the two share golden vectors (the parity test).
//
// Canonical workout JSON (what POST /workout accepts):
//   { "name":"4x8 Threshold", "ftp_w":285,
//     "segments":[ {"t":600,"label":"Warm-up","pct_ftp":0.55},
//                  {"t":480,"label":"Interval 1","power_w":250,"cadence_rpm":90},
//                  {"t":120,"label":"Recovery","power_w":90} ] }
// Target resolves power_w -> pct_ftp -> zone (the Segment.resolved_power_w precedence).

// One block of a workout. Target is one of power_w (abs, wins) / pct_ftp (fraction) / zone ("Z1".."Z7").
struct WkSegment {
    long durationS = 0;
    std::string label;
    int powerW = -1;       // absolute watts; -1 = not set
    float pctFtp = -1.0f;  // fraction of FTP, e.g. 0.88; -1 = not set
    std::string zone;      // Coggan zone id; "" = not set
    int cadenceRpm = -1;   // optional target cadence; -1 = none
};

// A representative %FTP for a Coggan zone (midpoint of the band) — so a zone-based segment resolves
// to watts on-device without the full zone table. Mirrors the Coggan 7-zone model.
inline float zonePct(const std::string& z) {
    if (z == "Z1") return 0.50f;
    if (z == "Z2") return 0.65f;
    if (z == "Z3") return 0.83f;
    if (z == "Z4") return 0.98f;
    if (z == "Z5") return 1.13f;
    if (z == "Z6") return 1.35f;
    if (z == "Z7") return 1.60f;
    return -1.0f;
}

// Resolve a segment's concrete target watts given the workout FTP. -1 = no target (free/coast).
inline int segmentTargetW(const WkSegment& s, int ftpW) {
    if (s.powerW >= 0) return s.powerW;
    if (s.pctFtp >= 0.0f && ftpW > 0) return (int)std::lround(s.pctFtp * ftpW);
    if (!s.zone.empty() && ftpW > 0) {
        const float p = zonePct(s.zone);
        if (p >= 0.0f) return (int)std::lround(p * ftpW);
    }
    return -1;
}

struct Workout {
    std::string name;
    int ftpW = 0;
    std::vector<WkSegment> segments;
    long totalS() const {
        long t = 0;
        for (const auto& s : segments) t += s.durationS;
        return t;
    }
};

// --- a tiny tolerant JSON reader for our fixed shape (no ArduinoJson dep) --------------------------
namespace wkjson {

// Find `"key"` then its ':' within [obj], return the index just after ':' (or npos).
inline size_t valuePos(const std::string& obj, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t k = obj.find(needle);
    if (k == std::string::npos) return std::string::npos;
    size_t c = obj.find(':', k + needle.size());
    return (c == std::string::npos) ? std::string::npos : c + 1;
}

// Read a string value for key (first occurrence in [obj]). Minimal unescape (\" and \\).
inline std::string str(const std::string& obj, const std::string& key) {
    size_t p = valuePos(obj, key);
    if (p == std::string::npos) return "";
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == '\n' || obj[p] == '\r')) p++;
    if (p >= obj.size() || obj[p] != '"') return "";
    std::string out;
    for (size_t i = p + 1; i < obj.size(); ++i) {
        char ch = obj[i];
        if (ch == '\\' && i + 1 < obj.size()) { out += obj[++i]; continue; }
        if (ch == '"') break;
        out += ch;
    }
    return out;
}

// Read a numeric value for key. Returns whether found; writes the parsed double to out.
inline bool num(const std::string& obj, const std::string& key, double& out) {
    size_t p = valuePos(obj, key);
    if (p == std::string::npos) return false;
    char* end = nullptr;
    const char* start = obj.c_str() + p;
    double v = std::strtod(start, &end);
    if (end == start) return false;  // no number after the colon
    out = v;
    return true;
}

}  // namespace wkjson

// Parse the canonical workout JSON. Tolerant of whitespace; on a malformed/empty document returns a
// Workout with no segments (the caller treats that as "no workout loaded"). Pure.
inline Workout parseWorkout(const std::string& json) {
    Workout w;
    w.name = wkjson::str(json, "name");
    double d = 0;
    if (wkjson::num(json, "ftp_w", d)) w.ftpW = (int)std::lround(d);

    // Isolate the segments array [...] so per-segment keys never read the top-level name/ftp_w.
    size_t a = json.find("\"segments\"");
    if (a == std::string::npos) return w;
    size_t lb = json.find('[', a);
    if (lb == std::string::npos) return w;
    // Walk balanced objects within the array.
    int depth = 0;
    size_t objStart = std::string::npos;
    for (size_t i = lb + 1; i < json.size(); ++i) {
        char ch = json[i];
        if (ch == ']' && depth == 0) break;
        if (ch == '{') {
            if (depth == 0) objStart = i;
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0 && objStart != std::string::npos) {
                const std::string obj = json.substr(objStart, i - objStart + 1);
                WkSegment s;
                if (wkjson::num(obj, "t", d)) s.durationS = (long)std::lround(d);
                s.label = wkjson::str(obj, "label");
                if (wkjson::num(obj, "power_w", d)) s.powerW = (int)std::lround(d);
                if (wkjson::num(obj, "pct_ftp", d)) s.pctFtp = (float)d;
                s.zone = wkjson::str(obj, "zone");
                if (wkjson::num(obj, "cadence_rpm", d)) s.cadenceRpm = (int)std::lround(d);
                if (s.durationS > 0) w.segments.push_back(s);
                objStart = std::string::npos;
            }
        }
    }
    return w;
}

// A snapshot of where the rider is, at `elapsedS` seconds since the workout started (running). The
// pure analogue of director.py's DirectorState; the seam owns the clock (start / pause / skip) and
// calls this with the accumulated running time.
struct WkState {
    bool finished = false;
    int segIndex = 0;          // active segment; == segments.size() once finished
    long segElapsedS = 0;
    long segRemainingS = 0;
    long totalElapsedS = 0;
    long totalRemainingS = 0;
    int targetW = -1;          // resolved target of the active segment; -1 = none / done
};

inline WkState workoutStateAt(const Workout& w, long elapsedS) {
    WkState st;
    const long total = w.totalS();
    if (elapsedS < 0) elapsedS = 0;
    st.totalElapsedS = elapsedS < total ? elapsedS : total;
    st.totalRemainingS = total - st.totalElapsedS;
    if (w.segments.empty() || elapsedS >= total) {
        st.finished = !w.segments.empty();
        st.segIndex = (int)w.segments.size();
        return st;
    }
    long acc = 0;
    for (size_t i = 0; i < w.segments.size(); ++i) {
        const long dur = w.segments[i].durationS;
        if (elapsedS < acc + dur) {
            st.segIndex = (int)i;
            st.segElapsedS = elapsedS - acc;
            st.segRemainingS = dur - st.segElapsedS;
            st.targetW = segmentTargetW(w.segments[i], w.ftpW);
            return st;
        }
        acc += dur;
    }
    return st;  // unreachable (elapsed < total handled above)
}

// Render the workout (profile, for the chart) + the live cursor as JSON for GET /workout. The web
// Workout screen draws the profile from `segments[]` (resolved target watts + duration + label) and
// the current/next blocks + clock from the top-level fields. Pure (host-tested).
inline std::string renderWorkoutJson(const Workout& w, const WkState& s, bool running, bool paused) {
    auto labelAt = [&](int i) -> std::string {
        return (i >= 0 && i < (int)w.segments.size()) ? w.segments[i].label : std::string();
    };
    const int nextIdx = s.segIndex + 1;
    std::string j = "{";
    j += "\"name\":\"" + jsonEscape(w.name) + "\"";
    j += ",\"ftp_w\":" + std::to_string(w.ftpW);
    j += ",\"loaded\":" + std::string(w.segments.empty() ? "false" : "true");
    j += ",\"running\":" + std::string(running ? "true" : "false");
    j += ",\"paused\":" + std::string(paused ? "true" : "false");
    j += ",\"finished\":" + std::string(s.finished ? "true" : "false");
    j += ",\"seg_index\":" + std::to_string(s.segIndex);
    j += ",\"seg_count\":" + std::to_string((int)w.segments.size());
    j += ",\"seg_label\":\"" + jsonEscape(labelAt(s.segIndex)) + "\"";
    j += ",\"seg_target_w\":" + std::to_string(s.targetW);
    j += ",\"seg_elapsed_s\":" + std::to_string(s.segElapsedS);
    j += ",\"seg_remaining_s\":" + std::to_string(s.segRemainingS);
    j += ",\"next_label\":\"" + jsonEscape(labelAt(nextIdx)) + "\"";
    j += ",\"next_target_w\":" +
         std::to_string(nextIdx < (int)w.segments.size() ? segmentTargetW(w.segments[nextIdx], w.ftpW) : -1);
    j += ",\"total_elapsed_s\":" + std::to_string(s.totalElapsedS);
    j += ",\"total_remaining_s\":" + std::to_string(s.totalRemainingS);
    j += ",\"segments\":[";
    for (size_t i = 0; i < w.segments.size(); ++i) {
        if (i) j += ",";
        const WkSegment& seg = w.segments[i];
        j += "{\"t\":" + std::to_string(seg.durationS);
        j += ",\"w\":" + std::to_string(segmentTargetW(seg, w.ftpW));
        j += ",\"label\":\"" + jsonEscape(seg.label) + "\"}";
    }
    j += "]}";
    return j;
}

}  // namespace sb20proxy
