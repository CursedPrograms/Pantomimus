// Pantomimus - takes: what was recorded is the tracking itself (the landmarks of every frame, with their times), so the
// smoothing, the solving and the clean-up can be changed afterwards and the take made again ("baked") at any frame
// rate: trimmed, smoothed, the planted feet held still (IK on the legs), made to loop.
#pragma once
#include "face.h"
#include "filter.h"
#include "hands.h"
#include "markers.h"
#include "pose.h"
#include "skeleton.h"
#include "solver.h"

#include <string>
#include <vector>

namespace Clip {

struct Take {
    std::string name;
    std::string source;                // what it was recorded from (camera, video file)
    std::vector<double> t;             // seconds from the take's start
    std::vector<Pose::Result> raw;     // the tracking, as it came from the models (not smoothed)
    std::vector<Hands::Result> hands;  // the hands (empty when they weren't tracked)
    std::vector<Face::Result> faces;   // the face (empty when it wasn't tracked): its blendshapes and the head's turn
    std::vector<Markers::Result> markers;   // the Sharpie markers (empty when they weren't tracked), without the blobs
    double in = 0, out = -1;           // the trimmed part, seconds (out < 0: to the end)
    double length() const { return t.empty() ? 0 : t.back(); }
    double outPoint() const { return out < 0 ? length() : std::min(out, length()); }
    size_t frames() const { return t.size(); }
};

struct Cleanup {
    float fps = 30;                    // the baked frame rate
    float smooth = 0;                  // frames (Gaussian sigma) the rotations and the root are smoothed over afterwards
    bool footLock = true;              // planted feet held where they were put down (IK on the legs)
    int minPlant = 4;                  // frames: shorter plants are ignored
    float lockBlend = 3;               // frames to ease in and out of a lock
    bool loop = false;                 // the end blended into the start
    float loopFrames = 10;             // how long the blend is
    bool inPlace = false;              // the root kept over the origin (across the floor)
    bool startAtOrigin = true;         // the root's first place moved to the origin
};

struct Baked {
    float fps = 30;
    std::vector<Solver::Frame> frames;
    bool empty() const { return frames.empty(); }
    double duration() const { return frames.size() / (double)fps; }
};

// the take solved again from its tracking: smoothing, solving, then trim, resample and clean-up
void bake(const Take& take, const Skel::Skeleton& s, const Filter::Settings& f, const Solver::Settings& so,
          const Hands::Settings& hs, const Cleanup& c, Baked& out);
// one baked frame at a time t (seconds, from the trimmed start), between frames
Skel::Pose sample(const Baked& b, double t, const Skel::Skeleton& s);
// the raw frame nearest t (seconds of the take)
size_t frameAt(const Take& take, double t);

// smoothing a run of poses (Gaussian over sigma frames), and the loop blend: used by bake, tested on their own
void smoothPoses(std::vector<Solver::Frame>& frames, float sigma);
void makeLoop(std::vector<Solver::Frame>& frames, int blend);
void lockFeet(std::vector<Solver::Frame>& frames, const Skel::Skeleton& s, int minPlant, float blend);

// a project: the takes (Pantomimus's own file, .pantomimus)
bool saveProject(const std::string& path, const std::vector<Take>& takes, std::string& err);
bool loadProject(const std::string& path, std::vector<Take>& takes, std::string& err);

// the face of a take, trimmed: ARKit's blendshapes and the head's turn as Live Link Face writes them (CSV: Timecode,
// BlendshapeCount, the 52 names with capitals, HeadYaw, HeadPitch, HeadRoll), resampled to fps
bool exportFaceCsv(const Take& take, float fps, const std::string& path, std::string& err);
// the Sharpie markers of a take, trimmed: each marker's place in the face's frame (eyes' corners 1 apart) a frame
bool exportMarkersCsv(const Take& take, const std::string& path, std::string& err);

}  // namespace Clip
