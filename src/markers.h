// Pantomimus - Sharpie markers: dots drawn on the face, tracked to correct the face mesh where it matters (brows,
// cheeks, lips, jaw). Each dot belongs to one of the mesh's landmarks: Pantomimus shows where to draw it, on the
// performer's own face. With a neutral face, calibrate: each dot is found near its landmark and their offset kept.
// Then every frame each dot is looked for where the mesh says it should be (its landmark plus the offset), snapped to
// the ink, and its landmark moved to the dot (minus the offset) before the blendshapes are read.
#pragma once
#include "face.h"
#include "image.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace Markers {

struct Spot {
    const char* name;     // "brow inner (right)": the performer's right
    int landmark;         // the face mesh landmark it belongs to
    float dx, dy;         // where to draw it from the landmark, in the face's frame (eyes' corners 1 apart; +x towards
                          // the performer's left, +y down): off the brows' hair and the lips, onto skin
};
constexpr int COUNT = 25;
extern const Spot LAYOUT[COUNT];

struct Blob { float x = 0, y = 0, r = 0, dark = 0; };   // frame pixels; dark: how much darker than round it (0..1)

struct Settings {
    bool on = false;
    float darkness = 0.22f;    // a dot is this much darker than the skin round it (0..1)
    float minDot = 0.006f;     // the smallest dot's radius, as a share of the face's width
    float maxDot = 0.035f;     // the largest
    float search = 0.06f;      // how far from where the mesh puts it a dot may be found (share of the face's width)
    float blend = 1.0f;        // how much a found dot moves its landmark (0: the markers are only tracked)
    float roundness = 0.45f;   // the least share of its box a dot fills (brows and hair are long, not round)
};

// what was found in one frame
struct Result {
    bool calibrated = false;
    int found = 0;
    bool seen[COUNT] = {};
    float x[COUNT] = {}, y[COUNT] = {};    // 0..1 of the frame
    float fx[COUNT] = {}, fy[COUNT] = {};  // in the face's own frame: the eyes' corners at (-0.5, 0) and (0.5, 0)
    std::vector<Blob> blobs;               // every candidate dot (for the overlay)
};

// a 2D similarity from the frame (pixels) to the face's frame and back: the eyes' outer corners at (-0.5, 0), (0.5, 0)
struct FaceFrame {
    float ox = 0, oy = 0, ax = 1, ay = 0, scale = 1;   // origin, unit x axis, interocular pixels
    void toFace(float px, float py, float& fx, float& fy) const;
    void toFrame(float fx, float fy, float& px, float& py) const;
};
FaceFrame faceFrame(const Face::Result& f, int frameW, int frameH);
// where spot i is drawn on this face (frame pixels): its landmark plus its shift
void spotPosition(const Face::Result& f, int i, int frameW, int frameH, float& px, float& py);

// dark round dots round the face (its eyes, mouth and nostrils left out)
std::vector<Blob> findDots(const Image& frame, const Face::Result& f, const Settings& s);

class Tracker {
public:
    // with a neutral face: each spot's dot found near its landmark; how many were found
    int calibrate(const Image& frame, const Face::Result& f, const Settings& s);
    bool calibrated() const { return calibrated_; }
    void clear() { calibrated_ = false; std::fill(std::begin(have_), std::end(have_), false); }
    // one frame: the dots found, and (blend > 0) the face's landmarks moved to them
    void track(const Image& frame, Face::Result& f, const Settings& s, Result& out);
    // the calibration (saved with the project)
    bool have(int i) const { return have_[i]; }
    void offset(int i, float& ox, float& oy) const { ox = offX_[i]; oy = offY_[i]; }
    void setCalibration(const bool have[COUNT], const float ox[COUNT], const float oy[COUNT]);

private:
    bool calibrated_ = false;
    bool have_[COUNT] = {};
    float offX_[COUNT] = {}, offY_[COUNT] = {};   // the dot minus its landmark, in the face's frame
};

}  // namespace Markers
