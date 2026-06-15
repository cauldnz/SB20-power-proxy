#pragma once
#include <vector>

#include "ICrankOutput.h"

namespace sb20proxy {

// A crank output that just records what it was asked to publish — for host tests of the
// relay (the firmware analogue of the Python BikeTwin's "what the twin saw").
class MockCrank : public ICrankOutput {
public:
    void begin() override { started = true; }
    void publishPower(const PowerReading& r) override {
        published.push_back(r);
        last = r;
    }

    bool started = false;
    PowerReading last;
    std::vector<PowerReading> published;
};

}  // namespace sb20proxy
