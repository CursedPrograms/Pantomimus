// Pantomimus - solving: BlazePose's world landmarks turned into the skeleton's bone rotations. The hips and the chest
// from the hip and shoulder lines and the body's up, the spine shared between them, the head from the ears and the
// nose; each limb bone swung to point where its landmarks point and twisted by a hint (the elbow's and knee's bend, the
// palm, the foot). Then where the body is: the feet kept on the ground, the root following the hips across the floor
// (from the picture and a guess of the camera's field of view), and which foot is planted.
#pragma once
#include "pose.h"
#include "skeleton.h"

namespace Solver {

enum RootMode { ROOT_IN_PLACE = 0, ROOT_FOLLOW = 1, ROOT_FULL = 2 };
enum Unseen { UNSEEN_REST = 0, UNSEEN_HOLD = 1 };

struct Settings {
    bool mirror = false;          // the performer as in a mirror (their left drives the skeleton's right)
    int rootMode = ROOT_FOLLOW;   // in place; across the floor; across the floor and up and down (jumps)
    bool groundLock = true;       // the lowest foot on the floor
    float rootScale = 1.0f;       // how far the root goes for a metre the performer goes
    float fov = 60;               // the camera's horizontal field of view, degrees (for the root's depth)
    float headPitch = 12;         // degrees: the nose sits this far under the ears' line when looking straight ahead
    float shrug = 0.5f;           // how much the shoulders follow (clavicles)
    float spineBend = 1.0f;       // how much of the chest's turn the spine takes (0: the chest alone)
    float forearmTwist = 1.0f;    // the palm turning the forearm
    float visibility = 0.5f;      // a limb whose landmarks are less sure than this is unseen
    int unseen = UNSEEN_REST;     // an unseen limb: as at rest, or held as last seen
    bool legs = true, arms = true, head = true, torso = true;   // which parts the tracking drives
    float contactHeight = 0.06f;  // metres: a foot this near the floor...
    float contactSpeed = 0.6f;    // ...and slower than this (m/s) is planted
    bool footLock = true;         // a planted foot doesn't slide (the root moves instead)
};

struct Frame {
    Skel::Pose pose;
    bool contact[2] = {false, false};   // left, right foot planted
    bool limbSeen[4] = {false, false, false, false};   // left arm, right arm, left leg, right leg
};

class Solver {
public:
    void reset();
    // the landmarks (already smoothed) onto s; t: seconds
    void solve(const Skel::Skeleton& s, const Pose::Result& r, double t, const Settings& set, Frame& out);
    // the origin: the performer's place now becomes the middle of the floor
    void recentre() { haveOrigin_ = false; }

private:
    void prepare(const Skel::Skeleton& s);
    const Skel::Skeleton* skel_ = nullptr;
    size_t skelBones_ = 0;
    float skelHeight_ = 0;
    Skel::Roles ro_;
    // rest points (skeleton world) standing for the landmarks
    M::V3 rest_[Pose::N];
    float restFloor_ = 0;
    Skel::Pose prev_;
    bool havePrev_ = false, haveOrigin_ = false;
    M::V3 origin_, lastRoot_;
    double lastT_ = 0;
    M::V3 lastFoot_[2], lockAt_[2];
    bool locked_[2] = {false, false};
};

// two-bone IK (thigh, calf; or upper arm, lower arm): the chain's end put at target, the bend kept towards pole
void twoBoneIK(const Skel::Skeleton& s, Skel::Pose& p, int upper, int lower, const M::V3& target, const M::V3& pole);
// the camera-space guess of a landmark set's place (metres): depth from the torso's size in the picture
M::V3 cameraPosition(const Pose::Result& r, float fovDeg);

}  // namespace Solver
