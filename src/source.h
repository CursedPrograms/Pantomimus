// Pantomimus - where the pictures come from: a webcam (Media Foundation on Windows, V4L2 on Linux) with its resolution
// and frame rate, a video file (Media Foundation; ffmpeg on Linux), or a still picture (for trying settings, and tests).
#pragma once
#include "image.h"

#include <memory>
#include <string>
#include <vector>

namespace Src {

struct Format {
    int w = 0, h = 0;
    float fps = 0;
    std::string kind;          // "MJPG", "YUY2", "NV12"...
};
struct Device {
    std::string name, id;      // id: the system's path for it
    std::vector<Format> formats;
};

// the cameras plugged in, with what each can do
std::vector<Device> cameras();

class Source {
public:
    virtual ~Source() = default;
    // the next frame (blocks until there is one); t: its time in seconds from the start
    virtual bool read(Image& out, double& t, std::string& err) = 0;
    virtual bool isFile() const { return false; }     // a video: every frame, at its own pace
    virtual bool isStill() const { return false; }
    virtual double duration() const { return 0; }     // seconds (video)
    virtual float fps() const { return 30; }
    virtual bool seek(double t) { (void)t; return false; }
    virtual std::string describe() const = 0;         // "Camera name, 1280 x 720, 30 fps"
};

// a camera by its index in cameras(); w, h, fps: the wanted mode (0: the camera's best)
std::unique_ptr<Source> openCamera(int index, int w, int h, float fps, std::string& err);
std::unique_ptr<Source> openVideo(const std::string& path, std::string& err);
std::unique_ptr<Source> openPicture(const std::string& path, std::string& err);
// a file by its extension: a picture or a video
std::unique_ptr<Source> openFile(const std::string& path, std::string& err);
bool isPicture(const std::string& path);

}  // namespace Src
