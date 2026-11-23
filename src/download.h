// Pantomimus - fetching the models on the first start: HTTPS to a file (WinHTTP on Windows, curl on Linux), with
// progress. The models and where they come from are listed here.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace Download {

// progress: bytes so far, total (0: unknown); return false to stop
using Progress = std::function<bool(long long done, long long total)>;
bool get(const std::string& url, const std::string& path, const Progress& progress, std::string& err);

struct Model {
    const char* file;      // the name in the models folder
    const char* url;
    long long size;        // about, bytes (for the progress before the server says)
    const char* what;      // what it is, for the screen
};
// BlazePose's detector and landmark models (MediaPipe, Apache-2.0; ONNX by Unity's inference-engine-blaze-pose) and
// MediaPipe's hand models
const std::vector<Model>& models();
// the ones a setup needs: the detector and the chosen landmark model (lite/full/heavy); hands: the hand models too;
// face: the face detector, mesh and blendshapes
std::vector<Model> needed(int poseModel, bool hands, bool face = false);
std::vector<Model> missing(const std::string& dir, int poseModel, bool hands, bool face = false);

// fetches what's missing into dir (and the models' licence beside them); progress 0..1 and what's coming in
bool fetch(const std::string& dir, const std::vector<Model>& list, std::atomic<float>& progress, std::string& status, std::string& err,
           const std::atomic<bool>& cancel);

}  // namespace Download
