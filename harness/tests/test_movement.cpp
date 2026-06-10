#include "catch.hpp"
#include <cmath>
#include "../../plugins/games/doom/movement.h"

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

TEST_CASE("forward intent advances along +x at angle 0", "[movement]") {
    doom::Pose p{0, 0, 0};
    doom::Intent in{1.0f, 0.0f, 0.0f, false};
    doom::MoveTuning t{100.0f, 100.0f, 1.0f};
    doom::Pose np = doom::integrate(p, in, t, 0.1f);
    REQUIRE(np.x == Catch::Approx(10.0f));
    REQUIRE(np.y == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(np.angle == Catch::Approx(0.0f));
}

TEST_CASE("turn rotates the heading by turnSpeed*dt", "[movement]") {
    doom::Pose p{0, 0, 0};
    doom::Intent in{0.0f, 0.0f, 1.0f, false};
    doom::MoveTuning t{100.0f, 100.0f, 2.0f};
    doom::Pose np = doom::integrate(p, in, t, 0.5f);
    REQUIRE(np.angle == Catch::Approx(1.0f));   // 2.0 rad/s * 0.5 s
    REQUIRE(np.x == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(np.y == Catch::Approx(0.0f).margin(1e-4));
}

TEST_CASE("strafe moves along the heading perpendicular", "[movement]") {
    // At angle 0 forward is (1,0); strafe-right is (sin,-cos) = (0,-1), so +strafe goes -y.
    doom::Pose p{0, 0, 0};
    doom::Intent in{0.0f, 1.0f, 0.0f, false};
    doom::MoveTuning t{100.0f, 100.0f, 1.0f};
    doom::Pose np = doom::integrate(p, in, t, 0.1f);
    REQUIRE(np.x == Catch::Approx(0.0f).margin(1e-4));
    REQUIRE(np.y == Catch::Approx(-10.0f));
}

TEST_CASE("zero intent is identity", "[movement]") {
    doom::Pose p{5.0f, 7.0f, 0.3f};
    doom::Intent in{0.0f, 0.0f, 0.0f, false};
    doom::MoveTuning t{100.0f, 100.0f, 1.0f};
    doom::Pose np = doom::integrate(p, in, t, 0.1f);
    REQUIRE(np.x == Catch::Approx(5.0f));
    REQUIRE(np.y == Catch::Approx(7.0f));
    REQUIRE(np.angle == Catch::Approx(0.3f));
}

TEST_CASE("dt scales the move linearly", "[movement]") {
    doom::Pose p{0, 0, 0};
    doom::Intent in{1.0f, 0.0f, 0.0f, false};
    doom::MoveTuning t{100.0f, 100.0f, 1.0f};
    doom::Pose a = doom::integrate(p, in, t, 0.1f);
    doom::Pose b = doom::integrate(p, in, t, 0.2f);
    REQUIRE(b.x == Catch::Approx(2.0f * a.x));
}
