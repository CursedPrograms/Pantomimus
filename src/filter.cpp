// Pantomimus - smoothing (see filter.h)
#include "filter.h"

#include <algorithm>
#include <cmath>

namespace Filter {

static float alpha(float cutoff, float dt) {
    const float tau = 1.0f / (2 * 3.14159265f * std::max(cutoff, 1e-4f));
    return 1.0f / (1.0f + tau / dt);
}

float OneEuro::apply(float v, double t, float valueScale) {
    if (!init_ || t <= t_) {
        if (!init_) { x_ = raw_ = v; dx_ = 0; init_ = true; t_ = t; return x_; }
        if (t <= t_) t = t_ + 1.0 / 30;   // the same timestamp twice: as if a frame passed
    }
    const float dt = (float)std::min(1.0, t - t_);
    t_ = t;
    const float d = (v - raw_) * valueScale / dt;   // MediaPipe: from the last raw value
    raw_ = v;
    dx_ += alpha(dCutoff, dt) * (d - dx_);
    const float cutoff = minCutoff + beta * std::fabs(dx_);
    x_ += alpha(cutoff, dt) * (v - x_);
    return x_;
}

void Landmarks::reset() {
    for (auto& a : img_) for (auto& f : a) f.reset();
    for (auto& a : world_) for (auto& f : a) f.reset();
    have_ = false;
}

void Landmarks::apply(Pose::Result& r, double t, const Settings& s) {
    if (!r.found) { reset(); return; }
    // the person's size in the picture (MediaPipe scales by the region): the value scale
    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
    for (int i = 0; i < Pose::N; ++i) {
        lo[0] = std::min(lo[0], r.image[i].x); hi[0] = std::max(hi[0], r.image[i].x);
        lo[1] = std::min(lo[1], r.image[i].y); hi[1] = std::max(hi[1], r.image[i].y);
    }
    const float size = std::max(1e-3f, ((hi[0] - lo[0]) + (hi[1] - lo[1])) * 0.5f);
    for (int i = 0; i < Pose::N; ++i) {
        const bool sure = r.visibility[i] >= s.visibility;
        if (sure) seen_[i] = t;
        if (have_ && !sure && t - seen_[i] < s.holdSeconds) {
            // held: the last good place, and the filters keep it
            r.image[i] = lastImg_[i]; r.world[i] = lastWorld_[i];
            continue;
        }
        if (s.on) {
            float* p = &r.image[i].x;
            for (int c = 0; c < 3; ++c) {
                OneEuro& f = img_[i][c];
                f.minCutoff = s.minCutoff; f.beta = s.beta; f.dCutoff = s.dCutoff;
                p[c] = f.apply(p[c], t, 1.0f / size);
            }
            float* w = &r.world[i].x;
            for (int c = 0; c < 3; ++c) {
                OneEuro& f = world_[i][c];
                f.minCutoff = s.worldMinCutoff; f.beta = s.worldBeta; f.dCutoff = s.worldDCutoff;
                w[c] = f.apply(w[c], t, 1);
            }
        }
        lastImg_[i] = r.image[i]; lastWorld_[i] = r.world[i];
    }
    have_ = true;
}

}  // namespace Filter
