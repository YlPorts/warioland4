#pragma once

#include <cstdint>

namespace wl4 {

// Exact nominal GBA video rate: 16,777,216 Hz / 280,896 clocks per frame.
// Decouples game ticks from the phone's variable refresh rate (60/90/120 Hz).
class FrameClock {
public:
    static constexpr double NanosecondsPerFrame =
        1000000000.0 * 280896.0 / 16777216.0;

    int Advance(int64_t timestampNs);
    void Reset();

private:
    int64_t previousNs_ = 0;
    double accumulatedNs_ = 0.0;
};

}  // namespace wl4
