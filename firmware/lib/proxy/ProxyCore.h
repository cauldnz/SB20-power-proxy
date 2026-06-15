#pragma once
#include "Correction.h"
#include "ICrankOutput.h"
#include "IPowerSource.h"
#include "PowerReading.h"

namespace sb20proxy {

// The pure relay (the C++ mirror of the Python ProxyCore): take each meter reading,
// apply the correction, publish it to the spoofed crank. No I/O of its own — the source
// and crank are interfaces — so it is host-unit-tested with mocks.
class ProxyCore {
public:
    ProxyCore(IPowerSource& source, ICrankOutput& crank, Correction correction = {})
        : source_(source), crank_(crank), correction_(correction) {}

    void begin() {
        crank_.begin();
        source_.onReading([this](const PowerReading& r) { forward(r); });
        source_.begin();
    }

    void loop() { source_.loop(); }

    // observability (for the OLED / tests)
    int forwarded() const { return forwarded_; }
    PowerReading lastSource() const { return lastSource_; }
    PowerReading lastOutput() const { return lastOutput_; }

private:
    void forward(const PowerReading& r) {
        lastSource_ = r;
        lastOutput_ = correction_.apply(r);
        ++forwarded_;
        crank_.publishPower(lastOutput_);
    }

    IPowerSource& source_;
    ICrankOutput& crank_;
    Correction correction_;
    int forwarded_ = 0;
    PowerReading lastSource_;
    PowerReading lastOutput_;
};

}  // namespace sb20proxy
