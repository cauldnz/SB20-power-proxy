#pragma once
#include <algorithm>
#include <vector>

#include "PowerReading.h"

namespace sb20proxy {

// A piecewise-linear power->factor curve: the C++ mirror of the Python GridTransform.
// Models a NON-linear meter error along the power curve (the XCadey velodrome case) —
// built from a meter-vs-reference calibration ride. The factor is linearly interpolated
// between breakpoints and held flat outside the range. Pure + header-only, so it is
// host-unit-tested with no hardware, same as the rest of lib/proxy.
struct CorrectionCurve {
    struct Point {
        float power_w;
        float factor;
    };
    std::vector<Point> points;  // kept sorted by power_w (see add())

    bool empty() const { return points.empty(); }

    // Add a (reported_power, factor) breakpoint and keep the curve sorted. Mirrors the
    // sort GridTransform does in its constructor so insertion order doesn't matter.
    void add(float power_w, float factor) {
        points.push_back({power_w, factor});
        std::sort(points.begin(), points.end(),
                  [](const Point& a, const Point& b) { return a.power_w < b.power_w; });
    }

    // Correction factor at a given reported power. Flat-held below the first / above the
    // last breakpoint; linearly interpolated between. Empty curve -> 1.0 (no correction).
    // Matches GridTransform.factor() exactly so firmware and Python agree on golden values.
    float factorAt(float power) const {
        if (points.empty()) return 1.0f;
        if (power <= points.front().power_w) return points.front().factor;
        if (power >= points.back().power_w) return points.back().factor;
        for (size_t i = 1; i < points.size(); ++i) {
            const Point& a = points[i - 1];
            const Point& b = points[i];
            if (power <= b.power_w) {
                float t = (b.power_w > a.power_w)
                              ? (power - a.power_w) / (b.power_w - a.power_w)
                              : 0.0f;
                return a.factor + t * (b.factor - a.factor);
            }
        }
        return 1.0f;
    }
};

// Meter-to-meter correction (the C++ mirror of transform.py). If `curve` is non-empty it
// takes precedence — the non-linear GridTransform path; otherwise the linear scale/offset
// (ScaleOffsetTransform) applies. `Correction{scale, offset}` aggregate-init still works
// (the curve defaults empty), so ProxyCore and main.cpp are unchanged.
struct Correction {
    float scale = 1.0f;
    float offset = 0.0f;
    CorrectionCurve curve;  // optional non-linear power->factor; wins when populated

    PowerReading apply(PowerReading r) const {
        float corrected = curve.empty()
                              ? (r.power_w * scale + offset)
                              : (r.power_w * curve.factorAt((float)r.power_w));
        int v = (int)(corrected + 0.5f);
        if (v < 0) v = 0;
        r.power_w = (int16_t)v;
        return r;
    }
};

}  // namespace sb20proxy
