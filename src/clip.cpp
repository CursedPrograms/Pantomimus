// Pantomimus - takes (see clip.h)
#include "clip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace Clip {

using M::Q;
using M::V3;

size_t frameAt(const Take& take, double t) {
    if (take.t.empty()) return 0;
    const auto it = std::lower_bound(take.t.begin(), take.t.end(), t);
    size_t i = (size_t)(it - take.t.begin());
    if (i >= take.t.size()) return take.t.size() - 1;
    if (i > 0 && t - take.t[i - 1] < take.t[i] - t) --i;
    return i;
}

static Solver::Frame mix(const Solver::Frame& a, const Solver::Frame& b, float k) {
    Solver::Frame r = a;
    for (size_t i = 0; i < r.pose.local.size() && i < b.pose.local.size(); ++i) r.pose.local[i] = M::slerp(a.pose.local[i], b.pose.local[i], k);
    r.pose.root = M::lerp(a.pose.root, b.pose.root, k);
    r.pose.hips = M::lerp(a.pose.hips, b.pose.hips, k);
    if (k >= 0.5f) { r.contact[0] = b.contact[0]; r.contact[1] = b.contact[1]; }
    return r;
}

void smoothPoses(std::vector<Solver::Frame>& f, float sigma) {
    if (sigma <= 0.05f || f.size() < 3) return;
    const int R = std::max(1, (int)std::ceil(sigma * 3));
    std::vector<float> k(2 * R + 1);
    for (int i = -R; i <= R; ++i) k[i + R] = std::exp(-0.5f * (i * i) / (sigma * sigma));
    const std::vector<Solver::Frame> src = f;
    const int n = (int)f.size();
    for (int i = 0; i < n; ++i) {
        const size_t nb = src[i].pose.local.size();
        for (size_t b = 0; b < nb; ++b) {
            // quaternions averaged on the same side as this frame's, then normalised (fine for nearby turns)
            Q acc(0, 0, 0, 0); float ws = 0;
            for (int j = -R; j <= R; ++j) {
                const int s = std::clamp(i + j, 0, n - 1);
                const Q q = M::sameHemisphere(src[s].pose.local[b], src[i].pose.local[b]);
                const float w = k[j + R];
                acc.w += q.w * w; acc.x += q.x * w; acc.y += q.y * w; acc.z += q.z * w; ws += w;
            }
            f[i].pose.local[b] = M::normalize(acc);
        }
        V3 root, hips; float ws = 0;
        for (int j = -R; j <= R; ++j) {
            const int s = std::clamp(i + j, 0, n - 1);
            root += src[s].pose.root * k[j + R]; hips += src[s].pose.hips * k[j + R]; ws += k[j + R];
        }
        f[i].pose.root = root / ws; f[i].pose.hips = hips / ws;
    }
}

void makeLoop(std::vector<Solver::Frame>& f, int blend) {
    const int n = (int)f.size();
    blend = std::min(blend, n / 2 - 1);
    if (blend < 1) return;
    // the clip shortened by the blend; its first frames fade in from the frames after its new end, so the last frame
    // runs on into the first. The tail's root is moved to start where the head's does (its travel kept).
    const int L = n - blend;
    std::vector<Solver::Frame> out(f.begin(), f.begin() + L);
    const V3 off = f[L].pose.root - f[0].pose.root;
    for (int i = 0; i < blend; ++i) {
        Solver::Frame tail = f[L + i];
        tail.pose.root -= off;
        const float k = (i + 1.0f) / (blend + 1.0f);
        const float s = k * k * (3 - 2 * k);
        out[i] = mix(tail, f[i], s);
    }
    f = out;
}

void lockFeet(std::vector<Solver::Frame>& f, const Skel::Skeleton& s, int minPlant, float blend) {
    const Skel::Roles ro = Skel::roles(s);
    const int n = (int)f.size();
    for (int side = 0; side < 2; ++side) {
        const int th = ro.thigh[side], ca = ro.calf[side], ft = ro.foot[side];
        if (th < 0 || ca < 0 || ft < 0) continue;
        int i = 0;
        while (i < n) {
            if (!f[i].contact[side]) { ++i; continue; }
            int j = i;
            while (j < n && f[j].contact[side]) ++j;
            if (j - i >= minPlant) {
                // where the ankle was while planted: its average, held for the whole plant
                std::vector<V3> ankle(j - i), knee(j - i);
                V3 mean;
                for (int k = i; k < j; ++k) {
                    Skel::World w; Skel::forward(s, f[k].pose, w);
                    ankle[k - i] = w.head[ft]; knee[k - i] = w.head[ca];
                    mean += w.head[ft];
                }
                mean = mean / (float)(j - i);
                const int ease = std::max(0, (int)std::round(blend));
                for (int k = std::max(0, i - ease); k < std::min(n, j + ease); ++k) {
                    float wgt = 1;
                    if (k < i) wgt = (k - (i - ease) + 1.0f) / (ease + 1.0f);
                    else if (k >= j) wgt = (j + ease - k) / (ease + 1.0f);
                    Skel::World w; Skel::forward(s, f[k].pose, w);
                    const V3 target = M::lerp(w.head[ft], mean, std::clamp(wgt, 0.0f, 1.0f));
                    // the knee's bend kept: the pole in front of the knee
                    const V3 pole = w.head[ca] + M::rotate(w.rot[ca], V3(0, 0, 0.5f));
                    Solver::twoBoneIK(s, f[k].pose, th, ca, target, pole);
                }
            }
            i = j;
        }
    }
}

void bake(const Take& take, const Skel::Skeleton& s, const Filter::Settings& fs_, const Solver::Settings& so, const Hands::Settings& hs,
          const Cleanup& c, Baked& out) {
    out = Baked();
    out.fps = std::clamp(c.fps, 1.0f, 240.0f);
    if (take.raw.empty() || s.empty()) return;
    // 1. the tracking smoothed and solved, frame by frame as it was recorded
    Filter::Landmarks filt;
    Solver::Solver solver;
    std::vector<Solver::Frame> solved(take.raw.size());
    for (size_t i = 0; i < take.raw.size(); ++i) {
        Pose::Result r = take.raw[i];
        filt.apply(r, take.t[i], fs_);
        solver.solve(s, r, take.t[i], so, solved[i]);
        if (hs.on && i < take.hands.size()) Hands::solveFingers(s, take.hands[i], so.mirror, hs, solved[i].pose);
    }
    // 2. the trimmed part at the baked frame rate
    const double a = std::clamp(take.in, 0.0, take.length()), b = std::max(a, take.outPoint());
    const int count = std::max(1, (int)std::floor((b - a) * out.fps + 1e-6) + 1);
    out.frames.resize(count);
    for (int k = 0; k < count; ++k) {
        const double t = a + k / (double)out.fps;
        const auto it = std::upper_bound(take.t.begin(), take.t.end(), t);
        size_t j = (size_t)(it - take.t.begin());
        if (j == 0) { out.frames[k] = solved[0]; continue; }
        if (j >= take.t.size()) { out.frames[k] = solved.back(); continue; }
        const double t0 = take.t[j - 1], t1 = take.t[j];
        out.frames[k] = mix(solved[j - 1], solved[j], t1 > t0 ? (float)((t - t0) / (t1 - t0)) : 0.0f);
    }
    // 3. the clean-up
    smoothPoses(out.frames, c.smooth);
    if (c.startAtOrigin || c.inPlace) {
        const V3 o = out.frames[0].pose.root - s.bones[0].head;
        for (auto& f : out.frames) { f.pose.root.x -= o.x; f.pose.root.z -= o.z; if (c.inPlace) { f.pose.root.x = s.bones[0].head.x; f.pose.root.z = s.bones[0].head.z; } }
    }
    if (c.footLock && !c.inPlace) lockFeet(out.frames, s, c.minPlant, c.lockBlend);
    if (c.loop) makeLoop(out.frames, (int)std::round(c.loopFrames));
}

Skel::Pose sample(const Baked& b, double t, const Skel::Skeleton& s) {
    Skel::Pose p; p.reset(s);
    if (b.frames.empty()) return p;
    const double f = std::clamp(t * b.fps, 0.0, (double)b.frames.size() - 1);
    const size_t i = (size_t)std::floor(f);
    if (i + 1 >= b.frames.size()) return b.frames.back().pose;
    return mix(b.frames[i], b.frames[i + 1], (float)(f - i)).pose;
}

// ---------------------------------------------------------------- the project file
// "PANTOMIMUS 3\n", then per take: name, source, in, out, frame count, has-hands, has-face, has-markers; then per frame
// its time and tracking. (Version 2 files, without the face, still open.)
static const char MAGIC[] = "PANTOMIMUS 3\n";
static const char MAGIC2[] = "PANTOMIMUS 2\n";

template <class T> static void put(std::ofstream& o, const T& v) { o.write((const char*)&v, sizeof v); }
template <class T> static bool take_(std::ifstream& i, T& v) { return (bool)i.read((char*)&v, sizeof v); }
static void putStr(std::ofstream& o, const std::string& s) { const uint32_t n = (uint32_t)s.size(); put(o, n); o.write(s.data(), n); }
static bool getStr(std::ifstream& i, std::string& s) { uint32_t n = 0; if (!take_(i, n) || n > (1u << 20)) return false; s.resize(n); return (bool)i.read(&s[0], n); }

static void putFace(std::ofstream& o, const Face::Result& f) {
    put(o, (uint8_t)f.found); put(o, f.score);
    o.write((const char*)f.weights, sizeof f.weights);
    put(o, f.head.w); put(o, f.head.x); put(o, f.head.y); put(o, f.head.z);
    put(o, f.yaw); put(o, f.pitch); put(o, f.roll);
}
static void getFace(std::ifstream& i, Face::Result& f) {
    uint8_t found = 0; take_(i, found); f.found = found != 0; take_(i, f.score);
    i.read((char*)f.weights, sizeof f.weights);
    take_(i, f.head.w); take_(i, f.head.x); take_(i, f.head.y); take_(i, f.head.z);
    take_(i, f.yaw); take_(i, f.pitch); take_(i, f.roll);
}
static void putMarkers(std::ofstream& o, const Markers::Result& m) {
    put(o, (uint8_t)m.calibrated); put(o, (int32_t)m.found);
    for (int k = 0; k < Markers::COUNT; ++k) { put(o, (uint8_t)m.seen[k]); put(o, m.x[k]); put(o, m.y[k]); put(o, m.fx[k]); put(o, m.fy[k]); }
}
static void getMarkers(std::ifstream& i, Markers::Result& m) {
    uint8_t c = 0; int32_t n = 0; take_(i, c); take_(i, n); m.calibrated = c != 0; m.found = n;
    for (int k = 0; k < Markers::COUNT; ++k) { uint8_t s = 0; take_(i, s); m.seen[k] = s != 0; take_(i, m.x[k]); take_(i, m.y[k]); take_(i, m.fx[k]); take_(i, m.fy[k]); }
}

bool saveProject(const std::string& path, const std::vector<Take>& takes, std::string& err) {
    std::ofstream o(fs::u8path(path), std::ios::binary);
    if (!o) { err = "Can't write " + path; return false; }
    o.write(MAGIC, sizeof MAGIC - 1);
    put(o, (uint32_t)takes.size());
    for (const Take& t : takes) {
        putStr(o, t.name); putStr(o, t.source);
        put(o, t.in); put(o, t.out);
        put(o, (uint32_t)t.raw.size());
        put(o, (uint8_t)(t.hands.size() == t.raw.size() ? 1 : 0));
        const bool hasFace = t.faces.size() == t.raw.size() && !t.raw.empty();
        const bool hasMarkers = t.markers.size() == t.raw.size() && !t.raw.empty();
        put(o, (uint8_t)hasFace); put(o, (uint8_t)hasMarkers);
        for (size_t k = 0; k < t.raw.size(); ++k) {
            const Pose::Result& r = t.raw[k];
            put(o, t.t[k]);
            put(o, (uint8_t)r.found); put(o, r.score); put(o, (int32_t)r.frameW); put(o, (int32_t)r.frameH);
            o.write((const char*)r.image, sizeof r.image); o.write((const char*)r.visibility, sizeof r.visibility);
            o.write((const char*)r.presence, sizeof r.presence); o.write((const char*)r.world, sizeof r.world);
            if (t.hands.size() == t.raw.size())
                for (const Hands::Hand& h : t.hands[k].hand) {
                    put(o, (uint8_t)h.found); put(o, h.score);
                    o.write((const char*)h.image, sizeof h.image); o.write((const char*)h.world, sizeof h.world);
                }
            if (hasFace) putFace(o, t.faces[k]);
            if (hasMarkers) putMarkers(o, t.markers[k]);
        }
    }
    if (!o) { err = "Couldn't write all of " + path; return false; }
    return true;
}

bool loadProject(const std::string& path, std::vector<Take>& takes, std::string& err) {
    std::ifstream i(fs::u8path(path), std::ios::binary);
    if (!i) { err = "Can't open " + path; return false; }
    char m[sizeof MAGIC - 1];
    if (!i.read(m, sizeof m) || (memcmp(m, MAGIC, sizeof m) != 0 && memcmp(m, MAGIC2, sizeof m) != 0)) { err = path + " isn't a Pantomimus project"; return false; }
    const bool v3 = memcmp(m, MAGIC, sizeof m) == 0;
    uint32_t n = 0;
    if (!take_(i, n) || n > 100000) { err = "A broken project"; return false; }
    std::vector<Take> out(n);
    for (Take& t : out) {
        uint32_t frames = 0; uint8_t hasHands = 0;
        if (!getStr(i, t.name) || !getStr(i, t.source) || !take_(i, t.in) || !take_(i, t.out) || !take_(i, frames) || !take_(i, hasHands) || frames > 10000000) {
            err = "A broken project"; return false;
        }
        uint8_t hasFace = 0, hasMarkers = 0;
        if (v3 && (!take_(i, hasFace) || !take_(i, hasMarkers))) { err = "A broken project"; return false; }
        t.t.resize(frames); t.raw.resize(frames);
        if (hasHands) t.hands.resize(frames);
        if (hasFace) t.faces.resize(frames);
        if (hasMarkers) t.markers.resize(frames);
        for (uint32_t k = 0; k < frames; ++k) {
            Pose::Result& r = t.raw[k];
            uint8_t found = 0; int32_t w = 0, h = 0;
            if (!take_(i, t.t[k]) || !take_(i, found) || !take_(i, r.score) || !take_(i, w) || !take_(i, h)) { err = "A broken project"; return false; }
            r.found = found != 0; r.frameW = w; r.frameH = h;
            i.read((char*)r.image, sizeof r.image); i.read((char*)r.visibility, sizeof r.visibility);
            i.read((char*)r.presence, sizeof r.presence); i.read((char*)r.world, sizeof r.world);
            if (hasHands)
                for (Hands::Hand& hd : t.hands[k].hand) {
                    uint8_t f = 0; take_(i, f); take_(i, hd.score); hd.found = f != 0;
                    i.read((char*)hd.image, sizeof hd.image); i.read((char*)hd.world, sizeof hd.world);
                }
            if (hasFace) getFace(i, t.faces[k]);
            if (hasMarkers) getMarkers(i, t.markers[k]);
            if (!i) { err = "The project ends early"; return false; }
        }
    }
    takes = std::move(out);
    return true;
}

// ---------------------------------------------------------------- the face's exports
// Live Link Face's columns, in its order (ARKit's names with capitals), then the head's and the eyes' turns
static const char* const LLF[] = {
    "EyeBlinkLeft", "EyeLookDownLeft", "EyeLookInLeft", "EyeLookOutLeft", "EyeLookUpLeft", "EyeSquintLeft", "EyeWideLeft",
    "EyeBlinkRight", "EyeLookDownRight", "EyeLookInRight", "EyeLookOutRight", "EyeLookUpRight", "EyeSquintRight", "EyeWideRight",
    "JawForward", "JawRight", "JawLeft", "JawOpen", "MouthClose", "MouthFunnel", "MouthPucker", "MouthRight", "MouthLeft",
    "MouthSmileLeft", "MouthSmileRight", "MouthFrownLeft", "MouthFrownRight", "MouthDimpleLeft", "MouthDimpleRight",
    "MouthStretchLeft", "MouthStretchRight", "MouthRollLower", "MouthRollUpper", "MouthShrugLower", "MouthShrugUpper",
    "MouthPressLeft", "MouthPressRight", "MouthLowerDownLeft", "MouthLowerDownRight", "MouthUpperUpLeft", "MouthUpperUpRight",
    "BrowDownLeft", "BrowDownRight", "BrowInnerUp", "BrowOuterUpLeft", "BrowOuterUpRight", "CheekPuff", "CheekSquintLeft",
    "CheekSquintRight", "NoseSneerLeft", "NoseSneerRight", "TongueOut"};

bool exportFaceCsv(const Take& take, float fps, const std::string& path, std::string& err) {
    if (take.faces.size() != take.raw.size() || take.raw.empty()) { err = "This take has no face: turn on the face and record it again"; return false; }
    std::ofstream o(fs::u8path(path));
    if (!o) { err = "Can't write " + path; return false; }
    o << "Timecode,BlendshapeCount";
    for (const char* n : LLF) o << ',' << n;
    o << ",HeadYaw,HeadPitch,HeadRoll,LeftEyeYaw,LeftEyePitch,LeftEyeRoll,RightEyeYaw,RightEyePitch,RightEyeRoll\n";
    int col[std::size(LLF)];
    for (size_t c = 0; c < std::size(LLF); ++c) col[c] = Face::blendshapeIndex(LLF[c]);
    fps = std::clamp(fps, 1.0f, 240.0f);
    const double t0 = take.in, t1 = take.outPoint();
    const int frames = std::max(1, (int)std::floor((t1 - t0) * fps + 1e-6) + 1);
    for (int f = 0; f < frames; ++f) {
        const double t = t0 + f / (double)fps;
        const Face::Result& r = take.faces[frameAt(take, t)];
        const int ff = f % (int)std::lround(fps), s = f / (int)std::lround(fps);
        char tc[64];
        snprintf(tc, sizeof tc, "%02d:%02d:%02d:%02d.000", s / 3600, (s / 60) % 60, s % 60, ff);
        o << tc << ',' << 61;
        for (size_t c = 0; c < std::size(LLF); ++c) o << ',' << (col[c] >= 0 && r.found ? r.weights[col[c]] : 0.0f);
        // the head's turn in Live Link Face's units (radians; yaw, pitch, roll), the eyes' left at 0
        const float k = M::PI / 180;
        o << ',' << (r.found ? r.yaw * k : 0) << ',' << (r.found ? r.pitch * k : 0) << ',' << (r.found ? r.roll * k : 0) << ",0,0,0,0,0,0\n";
    }
    if (!o) { err = "Couldn't write all of " + path; return false; }
    return true;
}

bool exportMarkersCsv(const Take& take, const std::string& path, std::string& err) {
    if (take.markers.size() != take.raw.size() || take.raw.empty()) { err = "This take has no markers: calibrate them and record it again"; return false; }
    std::ofstream o(fs::u8path(path));
    if (!o) { err = "Can't write " + path; return false; }
    o << "time";
    for (const Markers::Spot& s : Markers::LAYOUT) o << ",\"" << s.name << " x\",\"" << s.name << " y\"";
    o << "\n";
    for (size_t k = 0; k < take.raw.size(); ++k) {
        if (take.t[k] < take.in - 1e-9 || take.t[k] > take.outPoint() + 1e-9) continue;
        const Markers::Result& m = take.markers[k];
        o << (take.t[k] - take.in);
        for (int i = 0; i < Markers::COUNT; ++i) {
            if (m.seen[i]) o << ',' << m.fx[i] << ',' << m.fy[i];
            else o << ",,";
        }
        o << "\n";
    }
    if (!o) { err = "Couldn't write all of " + path; return false; }
    return true;
}

}  // namespace Clip
