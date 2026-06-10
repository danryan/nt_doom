#pragma once

namespace doom {

// Per-TU trig seam (render.h also declares this): host defines it with <cmath>, ARM with
// a rodata sine LUT. Redeclaring it here keeps movement.h free of the render dependency.
void cos_sin(float ang, float& c, float& s);

struct Pose       { float x, y, angle; };
struct Intent     { float forward, strafe, turn; bool fire; };   // forward/strafe/turn in [-1,1]
struct MoveTuning { float moveSpeed, strafeSpeed, turnSpeed; };   // units/sec, units/sec, rad/sec
struct MoveDelta  { float dx, dy; };

// Tank scheme: heading integrates the turn rate.
inline float turn_angle(float angle, float turn, float turnSpeed, float dt) {
    return angle + turn * turnSpeed * dt;
}

// World-space move delta for this dt. Forward is along (cos,sin); strafe-right is along
// the perpendicular (sin,-cos). Libm-free via cos_sin.
inline MoveDelta move_delta(float angle, const Intent& in, const MoveTuning& t, float dt) {
    float ca, sa; cos_sin(angle, ca, sa);
    float f = in.forward * t.moveSpeed;
    float s = in.strafe  * t.strafeSpeed;
    return { (f * ca + s * sa) * dt, (f * sa - s * ca) * dt };
}

// No-collision convenience: advance heading then position. The device path composes
// turn_angle + move_delta + collide_move so collision resolves the position per axis.
inline Pose integrate(const Pose& p, const Intent& in, const MoveTuning& t, float dt) {
    float a = turn_angle(p.angle, in.turn, t.turnSpeed, dt);
    MoveDelta d = move_delta(a, in, t, dt);
    return { p.x + d.dx, p.y + d.dy, a };
}

} // namespace doom
