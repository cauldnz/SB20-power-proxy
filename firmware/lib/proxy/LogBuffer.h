#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>  // std::move

namespace sb20proxy {

// Lowercase-hex-encode bytes for a log line (e.g. a raw control-point write the SB20 sent us).
// Pure + host-tested; the BLE peripheral uses it to log what a consumer writes for observation.
inline std::string toHex(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s += kHex[(data[i] >> 4) & 0xF];
        s += kHex[data[i] & 0xF];
    }
    return s;
}

// A fixed-capacity ring of recent log lines, kept in RAM so they can be served over HTTP
// (GET /log) — the C3 Super Mini's native-USB serial is unreliable, so the network is the
// dependable window in. Pure (no Arduino), host-tested. Oldest lines drop once capacity is
// reached; each line is length-capped so worst-case memory is bounded.
class LogBuffer {
public:
    static constexpr size_t kMaxLine = 160;

    explicit LogBuffer(size_t capacity = 60) : capacity_(capacity ? capacity : 1) {}

    void add(std::string line) {
        if (line.size() > kMaxLine) line.resize(kMaxLine);
        lines_.push_back(std::move(line));
        while (lines_.size() > capacity_) lines_.pop_front();
    }

    void clear() { lines_.clear(); }
    size_t count() const { return lines_.size(); }
    size_t capacity() const { return capacity_; }

    // Oldest-first, one line per entry (each terminated by '\n').
    std::string text() const {
        std::string out;
        for (const auto& l : lines_) {
            out += l;
            out += '\n';
        }
        return out;
    }

private:
    size_t capacity_;
    std::deque<std::string> lines_;
};

}  // namespace sb20proxy
