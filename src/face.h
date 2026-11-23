// Pantomimus - the face: MediaPipe's Face Landmarker (Apache-2.0) through ONNX Runtime. BlazeFace (short range) finds
// a face, the face landmark model gives 478 landmarks (the mesh and the irises), and the blendshape model turns 146 of
// them into ARKit's 52 blendshapes. The next frame's region comes from the landmarks, as MediaPipe tracks; when the
// body is tracked and the face is lost, the face's region comes from the body's face points first. The head's turn
// comes from the landmarks too.
#pragma once
#include "image.h"
#include "mathx.h"
#include "net.h"
#include "pose.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Face {

constexpr int N = 478;                 // landmarks: 468 of the mesh, then 5 for each iris
constexpr int BLENDSHAPES = 52;        // ARKit's, in MediaPipe's order (the first is "_neutral")
extern const char* const BLENDSHAPE_NAMES[BLENDSHAPES];
constexpr int SUBSET = 146;            // the landmarks the blendshape model reads
extern const int SUBSET_INDEX[SUBSET];

// landmarks that matter (MediaPipe's canonical face mesh)
enum : int {
    FOREHEAD = 10, CHIN = 152, NOSE_TIP = 1, NOSE_BRIDGE = 6,
    R_EYE_OUT = 33, R_EYE_IN = 133, L_EYE_IN = 362, L_EYE_OUT = 263,   // the performer's right and left
    MOUTH_R = 61, MOUTH_L = 291, LIP_TOP = 13, LIP_BOTTOM = 14,
    R_IRIS = 468, L_IRIS = 473
};
// the lines drawn for a face: its outline, the brows, the eyes, the nose and the lips (landmark pairs)
const std::vector<std::pair<int, int>>& contour();

struct Result {
    bool found = false;
    float score = 0;                   // the landmark model's "a face is here"
    bool detected = false;             // this frame ran the detector (else it tracked)
    Pose::Point image[N];              // x, y: 0..1 of the frame; z: depth on x's scale
    float weights[BLENDSHAPES] = {};   // 0..1
    M::Q head;                         // the head's turn (x right, y down, z away; facing the camera: identity)
    float yaw = 0, pitch = 0, roll = 0;   // degrees, the same turn
    Pose::Roi roi;
    double ms = 0;
};

struct Settings {
    bool on = false;
    float detectScore = 0.5f;          // the face detector's least score
    float presence = 0.5f;             // the landmark model's least "a face is here" to keep tracking
    float roiScale = 1.5f;             // the region round the face (MediaPipe: 1.5)
    float smooth = 0.4f;               // 0..1: landmarks smoothed from frame to frame
    float weightSmooth = 0.3f;         // 0..1: blendshapes smoothed from frame to frame
    float gain = 1.0f;                 // every blendshape times this (clamped to 0..1)
    bool eyesFromIris = true;          // eyeLook* from the irises' place in the eyes (steadier than the model's)
    bool fromBody = true;              // when the face is lost and the body is tracked, start from the body's face
};

constexpr const char* DETECTOR_FILE = "blaze_face_short_range.onnx";
constexpr const char* LANDMARK_FILE = "face_landmarks_detector.onnx";
constexpr const char* BLENDSHAPE_FILE = "face_blendshapes.onnx";

// BlazeFace short range's SSD anchors: 128 x 128, strides 8 16 16 16 (896), x y w h
std::vector<float> anchors();

class Tracker {
public:
    bool load(const std::string& dir, int threads, std::string& err);
    bool loaded() const { return det_.loaded() && lm_.loaded() && bs_.loaded(); }
    // one frame; body may be null (no body tracked). adjust (may be empty) changes the landmarks before the blendshapes
    // are read from them: the Sharpie markers correct the mesh there.
    bool process(const Image& frame, const Pose::Result* body, const Settings& s, Result& out, std::string& err,
                 const std::function<void(Result&)>& adjust = {});
    void reset() { roi_ = Pose::Roi(); have_ = false; }
    // the parts alone (tests)
    bool detect(const Image& frame, const Settings& s, Pose::Roi& roi, float& score, std::string& err);
    bool landmarks(const Image& frame, const Pose::Roi& roi, Result& out, std::string& err);
    bool blendshapes(const Result& r, int frameW, int frameH, float out[BLENDSHAPES], std::string& err);

private:
    Net::Model det_, lm_, bs_;
    std::vector<float> anchors_, buf_;
    Pose::Roi roi_;
    Result prev_;
    bool have_ = false;
    std::string dir_;
};

// the region for the next frame from the landmarks (MediaPipe's face_landmarks_to_roi)
Pose::Roi roiFromLandmarks(const Result& r, int frameW, int frameH, float scale);
// the head's turn from the landmarks (the eyes' line and the forehead-chin line)
void headTurn(Result& r, int frameW, int frameH);
// eyeLook* from where each iris sits between its eye's corners and lids
void eyesFromIrises(Result& r);
int blendshapeIndex(const std::string& name);   // -1 when it isn't one

}  // namespace Face
