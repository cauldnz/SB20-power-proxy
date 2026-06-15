#pragma once
#include "PowerReading.h"

namespace sb20proxy {

// Linear meter-to-meter correction (the C++ mirror of ScaleOffsetTransform).
// A fitted power->factor grid (GridTransform) lands behind this for the XCadey case.
struct Correction {
    float scale = 1.0f;
    float offset = 0.0f;

    PowerReading apply(PowerReading r) const {
        int v = (int)(r.power_w * scale + offset + 0.5f);
        if (v < 0) v = 0;
        r.power_w = (int16_t)v;
        return r;
    }
};

}  // namespace sb20proxy
