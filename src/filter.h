// Pantomimus - smoothing the landmarks: the One Euro filter (Casiez, Roussel, Vogel 2012), as MediaPipe smooths its
// pose (the picture's landmarks scaled by the person's size, the world's in metres), and what to do with a landmark the
// model isn't sure of (hold it where it was last seen, for a while).
#pragma once
#include "pose.h"

namespace Filter {

class OneEuro {
public:
    // minCutoff (Hz): how much a still point is smoothed (lower: smoother); beta: how fast it follows when it moves
    // (higher: less lag); dCutoff (Hz): the speed's own smoothing
    float minCutoff = 1, beta = 0, dCutoff = 1;
    float apply(float v, double t, float valueScale = 1);
    void reset() { init_ = false; }
    float value() const { return x_; }

private:
    bool init_ = false;
    float x_ = 0, dx_ = 0, raw_ = 0;
    double t_ = 0;
};

struct Settings {
    bool on = true;
    // the picture's landmarks (MediaPipe: 0.05, 80, 1, scaled by the person's size)
    float minCutoff = 0.05f, beta = 80, dCutoff = 1;
    // the world landmarks, metres (MediaPipe: 0.1, 40, 1)
    float worldMinCutoff = 0.1f, worldBeta = 40, worldDCutoff = 1;
    float visibility = 0.5f;    // a landmark under this is unsure
    float holdSeconds = 0.5f;   // an unsure landmark stays where it was last seen this long, then follows again
};

// the whole pose's landmarks through the filters
class Landmarks {
public:
    void apply(Pose::Result& r, double t, const Settings& s);
    void reset();

private:
    OneEuro img_[Pose::N][3], world_[Pose::N][3];
    Pose::Point lastImg_[Pose::N], lastWorld_[Pose::N];
    double seen_[Pose::N] = {};
    bool have_ = false;
};

}  // namespace Filter
