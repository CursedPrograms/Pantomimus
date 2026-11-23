// Pantomimus - the hands: MediaPipe Hands (Apache-2.0) through ONNX Runtime. Each hand's region comes from the body's
// wrist, index and pinky (as MediaPipe's holistic tracking does), the palm detector when the body can't say; the hand
// landmark model gives 21 landmarks in the picture and in metres. The fingers' bones are then turned to match.
#pragma once
#include "image.h"
#include "net.h"
#include "pose.h"
#include "skeleton.h"

namespace Hands {

constexpr int N = 21;   // MediaPipe Hands' landmarks: wrist, then 4 a finger (thumb, index, middle, ring, pinky)
extern const int BONES[][2];
extern const int BONE_COUNT;

struct Hand {
    bool found = false;
    float score = 0;
    Pose::Point image[N];     // 0..1 of the frame
    Pose::Point world[N];     // metres about the hand's middle, as MediaPipe gives them (x right, y down)
    Pose::Roi roi;
};
struct Result { Hand hand[2]; };   // [0] the performer's left, [1] right

struct Settings {
    bool on = false;
    float presence = 0.5f;     // the hand model's least "a hand is here"
    float roiScale = 2.7f;     // the region round the palm, as a multiple of its size from the body (MediaPipe: 2.7)
    float curl = 1.0f;         // how much of the fingers' bend goes onto the skeleton
    float spread = 1.0f;       // how much of their spread
    float smooth = 0.5f;       // 0..1: how much the fingers are smoothed from frame to frame
};

constexpr const char* DETECTOR_FILE = "hand_detector.onnx";
constexpr const char* LANDMARK_FILE = "hand_landmarks_detector.onnx";

class Tracker {
public:
    bool load(const std::string& dir, int threads, std::string& err);
    bool loaded() const { return lm_.loaded(); }
    // both hands, their regions from the body's landmarks (which must be found)
    bool process(const Image& frame, const Pose::Result& body, const Settings& s, Result& out, std::string& err);

private:
    bool landmarks(const Image& frame, const Pose::Roi& roi, Hand& h, std::string& err);
    Net::Model det_, lm_;
    std::vector<float> buf_;
    Result prev_;
};

// the fingers of a solved pose turned to the hands' landmarks (the hand bone's own turn is the body's)
void solveFingers(const Skel::Skeleton& s, const Result& r, bool mirror, const Settings& set, Skel::Pose& p);

}  // namespace Hands
