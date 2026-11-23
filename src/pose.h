// Pantomimus - BlazePose (MediaPipe, Apache-2.0) through ONNX Runtime: the pose detector finds a person, the landmark
// model (lite, full or heavy) gives 33 landmarks in the picture and in metres (world), and the next frame's region comes
// from the landmarks (two auxiliary points), as MediaPipe tracks; the detector runs again only when the person is lost.
#pragma once
#include "image.h"
#include "net.h"

#include <string>

namespace Pose {

constexpr int N = 33;   // BlazePose's landmarks
enum Landmark {
    NOSE = 0, L_EYE_IN, L_EYE, L_EYE_OUT, R_EYE_IN, R_EYE, R_EYE_OUT, L_EAR, R_EAR, MOUTH_L, MOUTH_R,
    L_SHOULDER, R_SHOULDER, L_ELBOW, R_ELBOW, L_WRIST, R_WRIST, L_PINKY, R_PINKY, L_INDEX, R_INDEX, L_THUMB, R_THUMB,
    L_HIP, R_HIP, L_KNEE, R_KNEE, L_ANKLE, R_ANKLE, L_HEEL, R_HEEL, L_FOOT, R_FOOT
};
const char* landmarkName(int i);
// the lines drawn between landmarks (pairs)
extern const int BONES[][2];
extern const int BONE_COUNT;

struct Point { float x = 0, y = 0, z = 0; };
struct Roi { float cx = 0, cy = 0, size = 0, rot = 0; bool valid = false; };   // pixels, radians

struct Result {
    bool found = false;
    float score = 0;              // the landmark model's "a person is here"
    bool detected = false;        // this frame ran the detector (else it tracked)
    Point image[N];               // x, y: 0..1 of the frame; z: depth on x's scale (smaller: nearer)
    float visibility[N] = {}, presence[N] = {};
    Point world[N];               // metres, the hips' middle at 0, x right, y down, z away (MediaPipe's)
    Roi roi;                      // where the landmark model looked
    int frameW = 0, frameH = 0;
    double ms = 0;                // how long it took
};

struct Settings {
    int model = 1;                // 0 lite, 1 full, 2 heavy
    float detectScore = 0.5f;     // the detector's least score
    float presence = 0.5f;        // the landmark model's least "a person is here" to keep tracking
    float roiScale = 1.25f;       // the region around the person (MediaPipe: 1.25)
    bool refine = true;           // landmarks refined on the heatmap (MediaPipe does)
    int threads = 0;              // ONNX Runtime's threads (0: its choice)
};

const char* modelFile(int model);   // "pose_landmarks_detector_full.onnx"...
constexpr const char* DETECTOR_FILE = "pose_detection.onnx";
constexpr const char* ANCHORS_FILE = "pose_anchors.csv";   // the detector's 2254 SSD anchors (made by anchors())
// MediaPipe's SSD anchors for the 224 x 224 pose detector (strides 8, 16, 32, 32, 32; two a cell), x y w h
std::vector<float> anchors();

class Tracker {
public:
    // dir: where the models are; loads the detector and the chosen landmark model
    bool load(const std::string& dir, const Settings& s, std::string& err);
    bool loaded() const { return det_.loaded() && lm_.loaded(); }
    int loadedModel() const { return model_; }
    // one frame; keeps the region for the next
    bool process(const Image& frame, const Settings& s, Result& out, std::string& err);
    void reset() { roi_ = Roi(); }   // the next frame runs the detector
    // the detector alone (tests): the best person's region, false if none
    bool detect(const Image& frame, const Settings& s, Roi& roi, float& score, std::string& err);

private:
    bool landmarks(const Image& frame, const Roi& roi, const Settings& s, Result& out, std::string& err);
    Net::Model det_, lm_;
    std::vector<float> anchors_;   // x, y, w, h per anchor
    int model_ = -1, threads_ = -1;
    std::string dir_;
    Roi roi_;
    std::vector<float> buf_;
};

}  // namespace Pose
