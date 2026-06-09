#pragma once
#include "movement.h"   // Intent

namespace doom {

// Single-pole IIR lowpass on the raw CV, applied before conditioning to kill jitter and
// zipper. k in (0,1]; larger is snappier.
inline float cv_lowpass(float prev, float v, float k) { return prev + (v - prev) * k; }

// Bipolar CV volts to a normalized [-1,1] intent. Deadzone rejects offset/noise near 0 V,
// the magnitude normalizes to full scale, and the square taper gives fine low-speed
// control. Sign-preserving and libm-free.
inline float cv_to_norm(float v, float dz, float vmax) {
    float s = (v < 0.0f) ? -1.0f : 1.0f;
    float m = (v < 0.0f ? -v : v) - dz;
    if (m <= 0.0f) return 0.0f;
    float n = m / (vmax - dz);
    if (n > 1.0f) n = 1.0f;
    return s * n * n;
}

struct InputConfig {
    int   fwdBus, turnBus, strafeBus, fireBus;   // 0-based bus indices
    float dz, vmax, lpK;                         // deadzone V, full-scale V, lowpass k
    float fireThresh;                            // fire gate threshold V
};

// One CV sample per axis (first frame of each configured bus), lowpassed into
// lpState[0..3] then conditioned into an Intent. Bus layout: busFrames[bus*numFrames+frame].
// Call once per game frame, never at audio rate.
inline Intent read_cv_intent(const float* busFrames, int numFrames,
                             const InputConfig& cfg, float lpState[4]) {
    auto sample = [&](int bus) -> float { return busFrames[bus * numFrames + 0]; };
    lpState[0] = cv_lowpass(lpState[0], sample(cfg.fwdBus),    cfg.lpK);
    lpState[1] = cv_lowpass(lpState[1], sample(cfg.turnBus),   cfg.lpK);
    lpState[2] = cv_lowpass(lpState[2], sample(cfg.strafeBus), cfg.lpK);
    lpState[3] = cv_lowpass(lpState[3], sample(cfg.fireBus),   cfg.lpK);
    Intent in;
    in.forward = cv_to_norm(lpState[0], cfg.dz, cfg.vmax);
    in.turn    = cv_to_norm(lpState[1], cfg.dz, cfg.vmax);
    in.strafe  = cv_to_norm(lpState[2], cfg.dz, cfg.vmax);
    in.fire    = lpState[3] > cfg.fireThresh;
    return in;
}

} // namespace doom
