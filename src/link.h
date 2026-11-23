// Pantomimus - the live link: the pose as it happens, to other programs.
//   Server: Pantomimus listens (127.0.0.1, port 47811 by default) and sends every client one JSON line a frame:
//     {"type":"skeleton","name":...,"bones":[{"name","parent","head":[x,y,z],"tail":[x,y,z]}...]}   when a client comes
//     {"type":"pose","time":t,"bones":[[qw,qx,qy,qz, hx,hy,hz]...],"contact":[l,r],"face":[52 weights]} each frame
//   ("face" only when the face is tracked; its names, ARKit's, come in the skeleton message as "faceNames")
//   (metres, Y up, the body facing +Z; each bone's turn from its rest in the world's axes and its head's place.)
//   The Blender add-on (blender/pantomimus_link.py) reads it onto an armature, and can record it.
//   Pygmalion: Pantomimus calls Pygmalion's socket server (its "getPose" call with a one-frame BVH, Mixamo's names) a
//   few times a second, so the person in Pygmalion moves with you.
#pragma once
#include "face.h"
#include "skeleton.h"

#include <string>
#include <vector>

namespace Link {

constexpr int DEFAULT_PORT = 47811;        // outside 5000-5999 (DREAM Robotics')
constexpr int PYGMALION_PORT = 12345;      // Pygmalion's socket server

class Server {
public:
    ~Server() { stop(); }
    bool start(int port, std::string& err);
    void stop();
    bool running() const;
    int clients() const;
    int port() const { return port_; }
    // the skeleton new clients are told about; then each pose
    void setSkeleton(const Skel::Skeleton& s, int naming);
    void send(const Skel::Skeleton& s, const Skel::Pose& p, double t, const bool contact[2], const Face::Result* face = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int port_ = 0;
};

class Pygmalion {
public:
    ~Pygmalion() { stop(); }
    void start(const std::string& host, int port, float rate);
    void stop();
    bool running() const;
    void send(const Skel::Skeleton& s, const Skel::Pose& p);   // the latest pose; sent at the rate
    std::string status() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// the JSON lines (tests read them back)
std::string skeletonMessage(const Skel::Skeleton& s, int naming);
std::string poseMessage(const Skel::Skeleton& s, const Skel::Pose& p, double t, const bool contact[2], const Face::Result* face = nullptr);

// a client (tests): connects and reads one line
bool readLine(const std::string& host, int port, std::string& line, int timeoutMs, std::string& err);

}  // namespace Link
