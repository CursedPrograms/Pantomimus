// Pantomimus - the skeleton (see skeleton.h)
#include "skeleton.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Skel {

Roles::Roles() {
    for (auto& s : finger) for (auto& f : s) for (int& b : f) b = -1;
}

float Skeleton::height() const {
    float lo = 1e9f, hi = -1e9f;
    for (const Bone& b : bones) { lo = std::min({lo, b.head.y, b.tail.y}); hi = std::max({hi, b.head.y, b.tail.y}); }
    return bones.empty() ? 0 : hi - lo;
}

bool load(Skeleton& s, const std::string& path, std::string& err) {
    std::ifstream f(fs::u8path(path));
    if (!f) { err = "Can't read " + path; return false; }
    s = Skeleton();
    s.name = fs::u8path(path).stem().u8string();
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        Bone b; std::string parent;
        ls >> b.name >> parent >> b.head.x >> b.head.y >> b.head.z >> b.tail.x >> b.tail.y >> b.tail.z;
        if (!ls) { err = "A bad line in " + path + ": " + line; return false; }
        if (parent != "-") {
            const int p = s.find(parent);
            if (p < 0) { err = "In " + path + ", " + b.name + "'s parent " + parent + " comes after it"; return false; }
            b.parent = p;
        }
        s.byName[b.name] = (int)s.bones.size();
        s.bones.push_back(b);
    }
    if (s.bones.empty()) { err = path + " has no bones"; return false; }
    s.hips = s.find("pelvis");
    if (s.hips < 0) s.hips = s.find("Hips");
    if (s.hips < 0) s.hips = s.bones.size() > 1 && s.bones[1].parent == 0 ? 1 : 0;
    return true;
}

void forward(const Skeleton& s, const Pose& p, World& w) {
    const size_t n = s.bones.size();
    w.rot.resize(n); w.head.resize(n); w.tail.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Bone& b = s.bones[i];
        const Q l = i < p.local.size() ? p.local[i] : Q();
        if (b.parent < 0) {
            w.rot[i] = l;
            w.head[i] = p.root + ((int)i == s.hips ? p.hips : V3());
        } else {
            const int q = b.parent;
            w.rot[i] = M::normalize(w.rot[q] * l);
            w.head[i] = w.head[q] + M::rotate(w.rot[q], b.head - s.bones[q].head + ((int)i == s.hips ? p.hips : V3()));
        }
        w.tail[i] = w.head[i] + M::rotate(w.rot[i], b.tail - b.head);
    }
}

void localFromWorld(const Skeleton& s, const std::vector<Q>& worldRot, Pose& p) {
    p.local.resize(s.bones.size());
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const int q = s.bones[i].parent;
        p.local[i] = q < 0 ? worldRot[i] : M::normalize(M::conj(worldRot[q]) * worldRot[i]);
    }
}

Skeleton scaled(const Skeleton& s, float k) {
    Skeleton r = s;
    for (Bone& b : r.bones) { b.head *= k; b.tail *= k; }
    return r;
}

Skeleton toTPose(const Skeleton& a) {
    const Roles r = roles(a);
    std::vector<Q> rot(a.bones.size());
    // each bone turns as its parent unless it's an arm bone, which turns to point straight out
    for (size_t i = 0; i < a.bones.size(); ++i) rot[i] = a.bones[i].parent < 0 ? Q() : rot[a.bones[i].parent];
    for (int side = 0; side < 2; ++side) {
        const V3 out(side == 0 ? 1.0f : -1.0f, 0, 0);
        const int chain[2] = {r.upperarm[side], r.lowerarm[side]};
        for (int c = 0; c < 2; ++c) {
            const int b = chain[c];
            if (b < 0) continue;
            const V3 d = M::normalize(a.bones[b].tail - a.bones[b].head);
            rot[b] = M::fromTo(d, out);
            for (size_t i = b + 1; i < a.bones.size(); ++i) {   // the children follow (hand, fingers)
                int p = a.bones[i].parent; bool under = false;
                while (p >= 0) { if (p == b) { under = true; break; } p = a.bones[p].parent; }
                if (under && (c == 1 || (int)i != r.lowerarm[side])) rot[i] = rot[b];
            }
        }
    }
    Pose p; p.reset(a);
    localFromWorld(a, rot, p);
    World w; forward(a, p, w);
    Skeleton t = a;
    for (size_t i = 0; i < t.bones.size(); ++i) { t.bones[i].head = w.head[i]; t.bones[i].tail = w.tail[i]; }
    t.name = a.name + " (T)";
    return t;
}

// Mixamo's names for the game-engine bones
static const std::map<std::string, std::string>& mixamoNames() {
    static const std::map<std::string, std::string> m = [] {
        std::map<std::string, std::string> r = {{"pelvis", "Hips"}, {"spine_01", "Spine"}, {"spine_02", "Spine1"}, {"spine_03", "Spine2"},
                                                {"neck_01", "Neck"}, {"head", "Head"}};
        const char* fing[] = {"thumb", "index", "middle", "ring", "pinky"};
        const char* Fing[] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
        for (int s = 0; s < 2; ++s) {
            const std::string l = s == 0 ? "_l" : "_r", S = s == 0 ? "Left" : "Right";
            r["clavicle" + l] = S + "Shoulder"; r["upperarm" + l] = S + "Arm"; r["lowerarm" + l] = S + "ForeArm"; r["hand" + l] = S + "Hand";
            r["thigh" + l] = S + "UpLeg"; r["calf" + l] = S + "Leg"; r["foot" + l] = S + "Foot"; r["ball" + l] = S + "ToeBase";
            for (int f = 0; f < 5; ++f)
                for (int k = 1; k <= 3; ++k) r[std::string(fing[f]) + "_0" + std::to_string(k) + l] = S + "Hand" + Fing[f] + std::to_string(k);
        }
        return r;
    }();
    return m;
}

std::string nameOf(const Skeleton& s, int bone, int naming) {
    const std::string& own = s.bones[bone].name;
    if (naming == NAME_OWN) return own;
    const auto& m = mixamoNames();
    const auto it = m.find(own);
    const std::string n = it == m.end() ? own : it->second;
    return naming == NAME_MIXAMO ? "mixamorig:" + n : n;
}

bool written(const Skeleton& s, int bone, int naming) {
    return !(naming != NAME_OWN && s.bones[bone].parent < 0 && s.bones[bone].name == "Root");
}

Roles roles(const Skeleton& s) {
    Roles r;
    auto f = [&](const char* n) { return s.find(n); };
    r.root = f("Root"); r.pelvis = f("pelvis");
    r.spine[0] = f("spine_01"); r.spine[1] = f("spine_02"); r.spine[2] = f("spine_03");
    r.neck = f("neck_01"); r.head = f("head");
    const char* fing[] = {"thumb", "index", "middle", "ring", "pinky"};
    for (int side = 0; side < 2; ++side) {
        const std::string l = side == 0 ? "_l" : "_r";
        r.clavicle[side] = f(("clavicle" + l).c_str()); r.upperarm[side] = f(("upperarm" + l).c_str());
        r.lowerarm[side] = f(("lowerarm" + l).c_str()); r.hand[side] = f(("hand" + l).c_str());
        r.thigh[side] = f(("thigh" + l).c_str()); r.calf[side] = f(("calf" + l).c_str());
        r.foot[side] = f(("foot" + l).c_str()); r.ball[side] = f(("ball" + l).c_str());
        for (int k = 0; k < 5; ++k)
            for (int j = 0; j < 3; ++j) r.finger[side][k][j] = f((std::string(fing[k]) + "_0" + std::to_string(j + 1) + l).c_str());
    }
    return r;
}

}  // namespace Skel
