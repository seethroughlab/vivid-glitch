#include "../../operators/audio/glitch_common/glitch_dsp.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    using glitch::RateDivision;

    check(std::fabs(glitch::division_multiplier(RateDivision::Whole) - 1.0f) < 1e-6f, "whole multiplier");
    check(std::fabs(glitch::division_multiplier(RateDivision::Quarter) - 0.25f) < 1e-6f, "quarter multiplier");
    check(std::fabs(glitch::division_multiplier(RateDivision::EighthTriplet) - (1.0f / 6.0f)) < 1e-6f, "eighth triplet multiplier");

    glitch::TempoTracker tracker;
    // Simulate first measured cycle of 48000 samples (one second at 48k).
    tracker.update_block(48000, false);
    tracker.update_block(256, true);
    check(tracker.has_period(), "tracker has measured period");
    check(tracker.trigger_period_samples == 48000, "tracker period sample count");

    uint32_t q_samples = glitch::resolve_tempo_locked_samples(
        true, 0.1f, static_cast<int>(RateDivision::Quarter), tracker, 48000, 1, 0);
    check(q_samples == 12000, "quarter-note slice samples");

    uint32_t fallback = glitch::resolve_tempo_locked_samples(
        false, 0.2f, static_cast<int>(RateDivision::Quarter), tracker, 48000, 1, 0);
    check(fallback == 9600, "fallback seconds path");

    if (g_failures == 0) {
        std::fprintf(stderr, "ALL PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d failures\n", g_failures);
    return 1;
}
