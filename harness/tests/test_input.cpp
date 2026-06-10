#include "catch.hpp"
#include <vector>
#include "../../plugins/games/doom/input.h"

// movement.h declares cos_sin; input.h does not call it, but the shared TU links cleanly
// without a definition only because nothing references it here.

TEST_CASE("cv_to_norm rejects the deadzone and preserves sign", "[input]") {
    REQUIRE(doom::cv_to_norm(0.05f, 0.1f, 1.0f) == Catch::Approx(0.0f));   // inside deadzone
    REQUIRE(doom::cv_to_norm(0.0f, 0.1f, 1.0f) == Catch::Approx(0.0f));
    REQUIRE(doom::cv_to_norm(-0.5f, 0.0f, 1.0f) == Catch::Approx(-0.125f)); // sign preserved (cubic)
}

TEST_CASE("cv_to_norm clamps at full scale", "[input]") {
    REQUIRE(doom::cv_to_norm(1.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));
    REQUIRE(doom::cv_to_norm(5.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));    // beyond vmax clamps
    REQUIRE(doom::cv_to_norm(-9.0f, 0.0f, 1.0f) == Catch::Approx(-1.0f));
}

TEST_CASE("cv_to_norm applies a cubic taper below full scale", "[input]") {
    // Half of full scale maps to 0.125, well below the linear 0.5: fine low-speed control.
    REQUIRE(doom::cv_to_norm(0.5f, 0.0f, 1.0f) == Catch::Approx(0.125f));
    REQUIRE(doom::cv_to_norm(0.5f, 0.0f, 1.0f) < 0.5f);
    REQUIRE(doom::cv_to_norm(1.0f, 0.0f, 1.0f) == Catch::Approx(1.0f));   // still full at max
    REQUIRE(doom::cv_to_norm(-0.5f, 0.0f, 1.0f) == Catch::Approx(-0.125f)); // sign preserved
}

TEST_CASE("cv_lowpass converges monotonically toward the input", "[input]") {
    float c = 0.0f;
    float prev = c;
    for (int i = 0; i < 10; ++i) {
        c = doom::cv_lowpass(c, 1.0f, 0.5f);
        REQUIRE(c > prev);     // monotone increasing toward 1
        REQUIRE(c <= 1.0f);
        prev = c;
    }
    REQUIRE(c == Catch::Approx(1.0f).margin(0.01f));
}

TEST_CASE("read_cv_intent maps configured buses to an intent", "[input]") {
    const int numFrames = 4;
    std::vector<float> bus(28 * numFrames, 0.0f);
    doom::InputConfig cfg{0, 1, 2, 3, /*dz*/0.0f, /*vmax*/5.0f, /*lpK*/1.0f, /*fireThresh*/1.0f};
    // bus 0 = forward 5 V (full), bus 1 = turn -5 V, bus 2 = strafe 0 V, bus 3 = fire 4 V.
    bus[0 * numFrames + 0] = 5.0f;
    bus[1 * numFrames + 0] = -5.0f;
    bus[2 * numFrames + 0] = 0.0f;
    bus[3 * numFrames + 0] = 4.0f;
    float lp[4] = {0, 0, 0, 0};
    doom::Intent in = doom::read_cv_intent(bus.data(), numFrames, cfg, lp);
    REQUIRE(in.forward == Catch::Approx(1.0f));
    REQUIRE(in.turn == Catch::Approx(-1.0f));
    REQUIRE(in.strafe == Catch::Approx(0.0f));
    REQUIRE(in.fire);
}
