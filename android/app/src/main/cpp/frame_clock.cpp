#include "frame_clock.h"

#include <algorithm>
#include <cmath>

namespace wl4 {
int FrameClock::Advance(int64_t timestampNs) {
    if (timestampNs <= 0) return 0;
    if (!previousNs_) {
        previousNs_ = timestampNs;
        return 1;
    }
    if (timestampNs <= previousNs_) {
        if (timestampNs < previousNs_) Reset();
        return 0;
    }
    const double delta = static_cast<double>(timestampNs - previousNs_);
    previousNs_ = timestampNs;
    // Long pauses must not turn into thousands of simulated ticks.
    if (delta > 250000000.0) {
        accumulatedNs_ = 0.0;
        return 1;
    }
    accumulatedNs_ += delta;
    const int ticks = static_cast<int>(accumulatedNs_ / NanosecondsPerFrame);
    if (ticks > 4) {
        accumulatedNs_ = std::fmod(accumulatedNs_, NanosecondsPerFrame);
        return 4;
    }
    accumulatedNs_ -= ticks * NanosecondsPerFrame;
    return ticks;
}

void FrameClock::Reset() {
    previousNs_ = 0;
    accumulatedNs_ = 0.0;
}
}  // namespace wl4
