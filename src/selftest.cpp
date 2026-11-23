// Pantomimus - the unit tests (`Pantomimus --selftest`): the maths, the skeleton, the solver on a pose it must give back,
// smoothing, takes and projects, the three exports read back, the live link, and BlazePose on the test picture.
#include "app.h"
#include "download.h"
#include "util.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

namespace SelfTest {

using M::Q;
using M::V3;

static int fails = 0, passes = 0;
static void check(bool ok, const char* what, const char* f = "", ...) {
    char b[512] = {0};
    va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    App::out(std::string(ok ? "  ok    " : "  FAIL  ") + what + (b[0] ? std::string(": ") + b : std::string()));
    (ok ? passes : fails)++;
}
static float qAngle(const Q& a, const Q& b) { return M::angleOf(M::normalize(M::conj(a) * b)) * 180 / M::PI; }
static float vAngle(const V3& a, const V3& b) { return std::acos(std::clamp(M::dot(M::normalize(a), M::normalize(b)), -1.0f, 1.0f)) * 180 / M::PI; }

static void maths() {
    App::out("maths");
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1, 1);
    float worstZXY = 0, worstXYZ = 0;
    for (int i = 0; i < 500; ++i) {
        const Q q = M::normalize(Q(u(rng), u(rng), u(rng), u(rng)));
        worstZXY = std::max(worstZXY, qAngle(q, M::fromEulerZXY(M::toEulerZXY(q))));
        worstXYZ = std::max(worstXYZ, qAngle(q, M::fromEulerXYZ(M::toEulerXYZ(q))));
    }
    check(worstZXY < 0.05f, "Euler ZXY round trip", "worst %.4f deg", worstZXY);
    check(worstXYZ < 0.05f, "Euler XYZ round trip", "worst %.4f deg", worstXYZ);
    const Q f = M::frameXY(V3(0, 0, 2), V3(1, 1, 0));
    check(vAngle(M::rotate(f, V3(1, 0, 0)), V3(0, 0, 1)) < 0.01f && vAngle(M::rotate(f, V3(0, 1, 0)), V3(1, 1, 0)) < 0.01f, "frames from axes");
    const Q ft = M::fromTo(V3(0, 1, 0), V3(1, 0, 0));
    check(vAngle(M::rotate(ft, V3(0, 1, 0)), V3(1, 0, 0)) < 0.01f, "shortest turn");
    const std::vector<float> a = Pose::anchors();
    check(a.size() == 2254 * 4, "the detector's anchors", "%zu", a.size() / 4);
    // against the anchors published with the models, if they're about
    std::ifstream csv(Util::join(App::st.modelsDir(), "anchors.csv"));
    if (csv) {
        float worst = 0; size_t n = 0; std::string line;
        while (std::getline(csv, line) && n < 2254) {
            float x, y; if (sscanf(line.c_str(), "%f,%f", &x, &y) != 2) continue;
            worst = std::max({worst, std::fabs(x - a[n * 4]), std::fabs(y - a[n * 4 + 1])}); ++n;
        }
        check(n == 2254 && worst < 1e-4f, "anchors match MediaPipe's", "%zu read, worst %.6f", n, worst);
    }
}

// the landmarks a posed skeleton would give (MediaPipe's world axes: x right, y down, z away; round the hips)
static Pose::Result landmarksOf(const Skel::Skeleton& s, const Skel::Pose& p) {
    Skel::World w; Skel::forward(s, p, w);
    const Skel::Roles ro = Skel::roles(s);
    Pose::Result r; r.found = true; r.score = 1; r.frameW = 640; r.frameH = 480;
    V3 P[Pose::N];
    for (int side = 0; side < 2; ++side) {
        P[Pose::L_SHOULDER + side] = w.head[ro.upperarm[side]];
        P[Pose::L_ELBOW + side] = w.head[ro.lowerarm[side]];
        P[Pose::L_WRIST + side] = w.head[ro.hand[side]];
        P[Pose::L_INDEX + side] = w.head[ro.finger[side][1][0]];
        P[Pose::L_PINKY + side] = w.head[ro.finger[side][4][0]];
        P[Pose::L_THUMB + side] = w.head[ro.finger[side][0][0]];
        P[Pose::L_HIP + side] = w.head[ro.thigh[side]];
        P[Pose::L_KNEE + side] = w.head[ro.calf[side]];
        P[Pose::L_ANKLE + side] = w.head[ro.foot[side]];
        P[Pose::L_FOOT + side] = w.head[ro.ball[side]];
        P[Pose::L_HEEL + side] = w.head[ro.foot[side]] + M::rotate(w.rot[ro.foot[side]], V3(0, -0.06f, -0.05f));
    }
    // the face round the head: ears either side, the nose in front and a little down (the solver's head tilt: 12 deg)
    const V3 hc = w.head[ro.head] + M::rotate(w.rot[ro.head], V3(0, 0.08f, 0));
    const float pr = 12 * M::PI / 180;
    P[Pose::L_EAR] = hc + M::rotate(w.rot[ro.head], V3(0.075f, 0, 0));
    P[Pose::R_EAR] = hc + M::rotate(w.rot[ro.head], V3(-0.075f, 0, 0));
    P[Pose::NOSE] = hc + M::rotate(w.rot[ro.head], V3(0, -std::sin(pr), std::cos(pr)) * 0.1f);
    for (int i = 1; i <= 6; ++i) P[i] = P[Pose::NOSE];
    P[Pose::MOUTH_L] = P[Pose::MOUTH_R] = P[Pose::NOSE];
    const V3 mid = (P[Pose::L_HIP] + P[Pose::R_HIP]) * 0.5f;
    for (int i = 0; i < Pose::N; ++i) {
        const V3 q = P[i] - mid;
        r.world[i] = {q.x, -q.y, -q.z};
        r.image[i] = {0.5f + q.x * 0.3f, 0.5f - q.y * 0.3f, 0};
        r.visibility[i] = r.presence[i] = 1;
    }
    return r;
}

static void skeletonAndSolver(const std::string& dir) {
    App::out("skeleton and solving");
    Skel::Skeleton s; std::string err;
    const bool ok = Skel::load(s, Util::exeDir() + "data/skeletons/game_engine.skel", err);
    check(ok && s.bones.size() == 53, "the game-engine skeleton", "%zu bones, %.2f m %s", s.bones.size(), s.height(), err.c_str());
    if (!ok) return;
    Skel::Pose rest; rest.reset(s);
    Skel::World w; Skel::forward(s, rest, w);
    float worst = 0;
    for (size_t i = 0; i < s.bones.size(); ++i) worst = std::max(worst, M::length(w.head[i] - s.bones[i].head) + M::length(w.tail[i] - s.bones[i].tail));
    check(worst < 1e-5f, "the rest pose is the measured skeleton", "%.6f", worst);
    const Skel::Roles ro = Skel::roles(s);
    const Skel::Skeleton t = Skel::toTPose(s);
    const V3 ua = t.bones[ro.upperarm[0]].tail - t.bones[ro.upperarm[0]].head, la = t.bones[ro.lowerarm[0]].tail - t.bones[ro.lowerarm[0]].head;
    check(vAngle(ua, V3(1, 0, 0)) < 0.5f && vAngle(la, V3(1, 0, 0)) < 0.5f, "the T pose's arms are level", "%.2f, %.2f deg", vAngle(ua, V3(1, 0, 0)), vAngle(la, V3(1, 0, 0)));
    check(Skel::nameOf(s, ro.upperarm[0], Skel::NAME_MIXAMO) == "mixamorig:LeftArm" && Skel::nameOf(s, s.hips, Skel::NAME_MIXAMO_BARE) == "Hips", "Mixamo's names");

    // the solver gives back a pose it's shown: turn bones, make the landmarks, solve, compare the bones' directions
    Skel::Pose p; p.reset(s);
    std::vector<Q> W(s.bones.size());
    for (size_t i = 0; i < s.bones.size(); ++i) W[i] = s.bones[i].parent < 0 ? Q() : W[s.bones[i].parent];
    auto turn = [&](int b, const Q& q) {
        const Q d = q;
        for (size_t i = b; i < s.bones.size(); ++i) {
            int a = (int)i; bool under = false;
            while (a >= 0) { if (a == b) { under = true; break; } a = s.bones[a].parent; }
            if (under) W[i] = M::normalize(d * W[i]);
        }
    };
    turn(ro.pelvis, M::axisAngle(V3(0, 1, 0), 0.4f));                    // facing a little left
    turn(ro.upperarm[0], M::axisAngle(V3(0, 0, 1), 0.9f));                 // the left arm out
    turn(ro.lowerarm[0], M::axisAngle(V3(1, 0, 0), -1.0f));                // its elbow bent
    turn(ro.upperarm[1], M::axisAngle(V3(1, 0, 0), -0.7f));                // the right arm forward
    turn(ro.thigh[1], M::axisAngle(V3(1, 0, 0), -0.6f));                   // the right knee up
    turn(ro.calf[1], M::axisAngle(V3(1, 0, 0), 0.9f));                     // and bent
    turn(ro.head, M::axisAngle(V3(0, 1, 0), 0.3f));
    Skel::localFromWorld(s, W, p);
    const Pose::Result r = landmarksOf(s, p);
    Solver::Solver sv; Solver::Settings set; set.rootMode = Solver::ROOT_IN_PLACE;
    Solver::Frame f;
    sv.solve(s, r, 0, set, f);
    Skel::World want, got; Skel::forward(s, p, want); Skel::forward(s, f.pose, got);
    float worstDir = 0; std::string worstBone;
    for (int b : {ro.upperarm[0], ro.lowerarm[0], ro.upperarm[1], ro.lowerarm[1], ro.thigh[0], ro.calf[0], ro.thigh[1], ro.calf[1]}) {
        const float a = vAngle(want.tail[b] - want.head[b], got.tail[b] - got.head[b]);
        if (a > worstDir) { worstDir = a; worstBone = s.bones[b].name; }
    }
    check(worstDir < 2.0f, "the solver points the limbs where they were", "worst %.2f deg (%s)", worstDir, worstBone.c_str());
    check(qAngle(want.rot[ro.pelvis], got.rot[ro.pelvis]) < 2.0f, "the hips' turn", "%.2f deg", qAngle(want.rot[ro.pelvis], got.rot[ro.pelvis]));
    check(qAngle(want.rot[ro.head], got.rot[ro.head]) < 3.0f, "the head's turn", "%.2f deg", qAngle(want.rot[ro.head], got.rot[ro.head]));
    // the bent elbow twists the upper arm right: the elbow's plane is kept
    const float tw = qAngle(want.rot[ro.lowerarm[0]], got.rot[ro.lowerarm[0]]);
    check(tw < 4.0f, "the forearm's turn (twist included)", "%.2f deg", tw);
    float lo = 1e9f; for (int side = 0; side < 2; ++side) lo = std::min({lo, got.tail[ro.foot[side]].y, got.head[ro.ball[side]].y});
    check(std::fabs(lo - std::min(s.bones[ro.foot[0]].tail.y, s.bones[ro.ball[0]].head.y)) < 0.01f, "the feet on the floor", "lowest %.3f m", lo);
    // mirrored: the performer's left arm moves the skeleton's right
    set.mirror = true;
    Solver::Solver sv2; Solver::Frame fm; sv2.solve(s, r, 0, set, fm);
    Skel::World gm; Skel::forward(s, fm.pose, gm);
    const V3 l = want.tail[ro.lowerarm[0]] - want.head[ro.lowerarm[0]], rr = gm.tail[ro.lowerarm[1]] - gm.head[ro.lowerarm[1]];
    check(vAngle(V3(-l.x, l.y, l.z), rr) < 3.0f, "mirroring", "%.2f deg", vAngle(V3(-l.x, l.y, l.z), rr));
    // an occluded arm is held, not snapped straight: solve the bent-arm frame, then the same frame with the left arm's
    // landmarks hidden; with UNSEEN_HOLD (the default) the elbow keeps its bend
    {
        Solver::Solver sh; Solver::Settings hs; hs.rootMode = Solver::ROOT_IN_PLACE;
        Solver::Frame f1; sh.solve(s, r, 0, hs, f1);        // arm seen: sets the solver's "previous"
        Pose::Result hidden = r;
        for (int i : {Pose::L_SHOULDER, Pose::L_ELBOW, Pose::L_WRIST, Pose::L_INDEX, Pose::L_PINKY, Pose::L_THUMB}) hidden.visibility[i] = hidden.presence[i] = 0.1f;
        Solver::Frame f2; sh.solve(s, hidden, 0.033, hs, f2);
        Skel::World g1, g2; Skel::forward(s, f1.pose, g1); Skel::forward(s, f2.pose, g2);
        const float held = vAngle(g2.tail[ro.lowerarm[0]] - g2.head[ro.lowerarm[0]], g1.tail[ro.lowerarm[0]] - g1.head[ro.lowerarm[0]]);
        // the bent forearm vs. the rest (straight-down) forearm: holding must stay far nearer the bent one
        Skel::Pose restp; restp.reset(s); Skel::World gr; Skel::forward(s, restp, gr);
        const float straight = vAngle(g2.tail[ro.lowerarm[0]] - g2.head[ro.lowerarm[0]], gr.tail[ro.lowerarm[0]] - gr.head[ro.lowerarm[0]]);
        check(held < 5.0f && straight > 20.0f, "an occluded arm holds its bend (not straight)", "held %.1f deg from bent, %.1f from rest", held, straight);
    }
    // two-bone IK reaches
    Skel::Pose ik = f.pose;
    const V3 target = got.tail[ro.calf[0]] + V3(0.05f, 0.1f, 0.08f);
    Solver::twoBoneIK(s, ik, ro.thigh[0], ro.calf[0], target, got.head[ro.calf[0]] + V3(0, 0, 0.5f));
    Skel::World wi; Skel::forward(s, ik, wi);
    check(M::length(wi.tail[ro.calf[0]] - target) < 0.002f, "two-bone IK reaches", "%.4f m off", M::length(wi.tail[ro.calf[0]] - target));
    (void)dir;
}

static void filters() {
    App::out("smoothing");
    Filter::OneEuro f; f.minCutoff = 1; f.beta = 0; f.dCutoff = 1;
    float v = 0;
    for (int i = 0; i < 300; ++i) v = f.apply(1.0f, i / 30.0);
    check(std::fabs(v - 1) < 1e-3f, "One Euro settles", "%.5f", v);
    Filter::OneEuro g; g.minCutoff = 0.5f; g.beta = 0;
    g.apply(0, 0);
    const float first = g.apply(1, 1 / 30.0);
    check(first > 0 && first < 0.5f, "One Euro smooths a jump", "%.3f", first);
    Filter::OneEuro h; h.minCutoff = 0.5f; h.beta = 50;
    h.apply(0, 0);
    const float fast = h.apply(1, 1 / 30.0);
    check(fast > first, "the speed makes it follow", "%.3f > %.3f", fast, first);
}

static Clip::Take makeTake(const Skel::Skeleton& s, int frames) {
    Clip::Take t; t.name = "Test"; t.source = "selftest";
    const Skel::Roles ro = Skel::roles(s);
    for (int i = 0; i < frames; ++i) {
        Skel::Pose p; p.reset(s);
        std::vector<Q> W(s.bones.size());
        for (size_t k = 0; k < s.bones.size(); ++k) W[k] = s.bones[k].parent < 0 ? Q() : W[s.bones[k].parent];
        const float a = std::sin(i * 0.2f) * 0.6f;
        W[ro.upperarm[0]] = W[ro.lowerarm[0]] = W[ro.hand[0]] = M::axisAngle(V3(0, 0, 1), a);
        Skel::localFromWorld(s, W, p);
        t.t.push_back(i / 30.0);
        t.raw.push_back(landmarksOf(s, p));
    }
    return t;
}

static bool readFile(const std::string& p, std::string& d) { std::ifstream f(std::filesystem::u8path(p), std::ios::binary); std::ostringstream o; o << f.rdbuf(); d = o.str(); return (bool)f; }

static void takesAndExports() {
    App::out("takes and exports");
    const Skel::Skeleton& s = App::st.skel;
    Clip::Take t = makeTake(s, 60);
    Clip::Baked b;
    Clip::Cleanup c; c.fps = 30;
    Clip::bake(t, s, Filter::Settings(), Solver::Settings(), Hands::Settings(), c, b);
    check(b.frames.size() == 60, "baking a take", "%zu frames", b.frames.size());
    t.in = 0.5; t.out = 1.5;
    Clip::bake(t, s, Filter::Settings(), Solver::Settings(), Hands::Settings(), c, b);
    check(b.frames.size() == 31, "trimmed", "%zu frames", b.frames.size());
    c.fps = 60;
    Clip::bake(t, s, Filter::Settings(), Solver::Settings(), Hands::Settings(), c, b);
    check(b.frames.size() == 61, "resampled to 60 fps", "%zu frames", b.frames.size());
    std::vector<Solver::Frame> fr = b.frames;
    Clip::makeLoop(fr, 10);
    const float seam = qAngle(fr.back().pose.local[Skel::roles(s).upperarm[0]], fr.front().pose.local[Skel::roles(s).upperarm[0]]);
    check(fr.size() == 51, "loop", "%zu frames, the seam %.2f deg", fr.size(), seam);
    std::vector<Solver::Frame> sm = b.frames;
    Clip::smoothPoses(sm, 2);
    check(sm.size() == b.frames.size(), "smoothing keeps the frames");
    // the project
    const std::string pj = Util::join(Util::tempDir(), "selftest.pantomimus");
    std::string err;
    std::vector<Clip::Take> back;
    const bool saved = Clip::saveProject(pj, {t, t}, err), loaded = Clip::loadProject(pj, back, err);
    check(saved && loaded && back.size() == 2 && back[1].raw.size() == t.raw.size() && back[0].in == 0.5 &&
              std::fabs(back[1].raw[7].world[15].x - t.raw[7].world[15].x) < 1e-7f, "project saved and opened", "%s", err.c_str());
    // the exports, read back
    c.fps = 30; t.in = 0; t.out = -1;
    Clip::bake(t, s, Filter::Settings(), Solver::Settings(), Hands::Settings(), c, b);
    for (int naming : {Skel::NAME_OWN, Skel::NAME_MIXAMO}) {
        Export::Options o; o.naming = naming;
        const std::string base = Util::join(Util::tempDir(), naming ? "selftest_mixamo" : "selftest");
        bool ok = Export::saveBvh(s, b, base + ".bvh", o, err);
        std::string d; readFile(base + ".bvh", d);
        const size_t joints = [&] { size_t n = 0, at = 0; while ((at = d.find("JOINT ", at)) != std::string::npos) { ++n; ++at; } return n; }();
        const size_t motion = d.find("Frames: 60");
        check(ok && motion != std::string::npos && joints == 51, naming ? "BVH (Mixamo)" : "BVH", "%zu joints + the root, %s", joints, motion != std::string::npos ? "60 frames" : "frames?");
        // the BVH's frame lines carry 6 + 3 a joint numbers
        size_t lineStart = d.find('\n', d.find("Frame Time:")) + 1;
        const std::string line = d.substr(lineStart, d.find('\n', lineStart) - lineStart);
        std::istringstream ls(line); size_t n = 0; float x; while (ls >> x) ++n;
        check(n == 6 + joints * 3, "BVH channels", "%zu numbers a frame", n);
        ok = Export::saveGlb(s, b, base + ".glb", o, err);
        readFile(base + ".glb", d);
        uint32_t magic = 0, len = 0, jlen = 0;
        if (d.size() > 20) { memcpy(&magic, d.data(), 4); memcpy(&len, d.data() + 8, 4); memcpy(&jlen, d.data() + 12, 4); }
        const std::string json = d.size() > 20 + jlen ? d.substr(20, jlen) : "";
        check(ok && magic == 0x46546C67 && len == d.size() && json.find("\"animations\"") != std::string::npos && json.find("\"skins\"") != std::string::npos,
              naming ? "glTF binary (Mixamo)" : "glTF binary", "%zu bytes", d.size());
        ok = Export::saveFbx(s, b, base + ".fbx", o, err);
        readFile(base + ".fbx", d);
        check(ok && d.compare(0, 18, "Kaydara FBX Binary") == 0 && d.find("AnimationCurve") != std::string::npos && d.find("LimbNode") != std::string::npos,
              naming ? "FBX (Mixamo)" : "FBX", "%zu bytes", d.size());
    }
    // the live link
    const bool contact[2] = {true, false};
    const std::string sk = Link::skeletonMessage(s, Skel::NAME_OWN), pm = Link::poseMessage(s, b.frames[3].pose, 0.1, contact);
    check(sk.find("\"skeleton\"") != std::string::npos && pm.find("\"contact\":[1,0]") != std::string::npos, "live link messages");
    Link::Server server; std::string line;
    const bool up = server.start(47899, err);
    server.setSkeleton(s, Skel::NAME_OWN);
    const bool got = up && Link::readLine("127.0.0.1", 47899, line, 3000, err);
    server.stop();
    check(got && line + "\n" == sk, "the server sends the skeleton", "%s", err.c_str());
}

static void tracking(const std::string& testsDir) {
    App::out("tracking (BlazePose)");
    std::string err;
    const bool rt = Net::runtime(err);
    check(rt, "ONNX Runtime", "%s", rt ? Net::runtimeVersion().c_str() : err.c_str());
    const std::string dir = App::st.modelsDir();
    if (!Download::missing(dir, 1, false).empty()) { App::out("  (the models aren't in " + dir + ": tracking not tested)"); return; }
    Image im;
    if (!Img::load(Util::join(testsDir, "person.jpg"), im, err)) { check(false, "the test picture", "%s", err.c_str()); return; }
    for (int model = 0; model < 3; ++model) {
        if (!Util::fileExists(Util::join(dir, Pose::modelFile(model)))) continue;
        Pose::Tracker tr; Pose::Settings ps; ps.model = model;
        if (!tr.load(dir, ps, err)) { check(false, "loading the models", "%s", err.c_str()); return; }
        Pose::Result r;
        tr.process(im, ps, r, err);
        Pose::Result r2;
        tr.process(im, ps, r2, err);   // the second frame tracks from the first's region
        const float noseY = r2.image[0].y, hipY = (r2.image[23].y + r2.image[24].y) / 2, ankleY = (r2.image[27].y + r2.image[28].y) / 2;
        const bool sane = r2.found && noseY < hipY && hipY < ankleY && r2.image[11].x > r2.image[12].x;
        const char* names[3] = {"lite", "full", "heavy"};
        check(r.found && r.detected && !r2.detected && sane, (std::string("BlazePose ") + names[model]).c_str(),
              "score %.3f, nose %.2f < hips %.2f < ankles %.2f, %.0f ms", r2.score, noseY, hipY, ankleY, r2.ms);
        const float shoulders = std::hypot(r2.world[11].x - r2.world[12].x, r2.world[11].y - r2.world[12].y);
        check(shoulders > 0.2f && shoulders < 0.55f, "world landmarks in metres", "shoulders %.2f m apart", shoulders);
    }
}

// a round dark dot inked onto a picture (a Sharpie mark): soft-edged, like ink on skin
static void inkDot(Image& im, float cx, float cy, float r) {
    for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); ++y)
        for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); ++x) {
            if (x < 0 || y < 0 || x >= im.w || y >= im.h) continue;
            const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            const float a = std::clamp(r + 0.5f - d, 0.0f, 1.0f) * 0.85f;
            uint8_t* p = &im.rgb[((size_t)y * im.w + x) * 3];
            for (int c = 0; c < 3; ++c) p[c] = (uint8_t)(p[c] * (1 - a) + 25 * a);
        }
}

static void face(const std::string& testsDir) {
    App::out("face (Face Landmarker) and Sharpie markers");
    const std::vector<float> a = Face::anchors();
    check(a.size() == 896 * 4, "the face detector's anchors", "%zu", a.size() / 4);
    check(Face::blendshapeIndex("jawOpen") == 25 && Face::blendshapeIndex("MOUTHSMILELEFT") == 44, "blendshape names");
    std::string err;
    const std::string dir = App::st.modelsDir();
    if (!Util::fileExists(Util::join(dir, Face::LANDMARK_FILE)) || !Util::fileExists(Util::join(dir, Face::BLENDSHAPE_FILE)) ||
        !Util::fileExists(Util::join(dir, Face::DETECTOR_FILE))) {
        App::out("  (the face models aren't in " + dir + ": the face isn't tested)");
        return;
    }
    Image im;
    if (!Img::load(Util::join(testsDir, "face.jpg"), im, err)) { check(false, "the face picture", "%s", err.c_str()); return; }
    Face::Tracker tr;
    if (!tr.load(dir, 0, err)) { check(false, "loading the face models", "%s", err.c_str()); return; }
    Face::Settings fs_; fs_.smooth = 0; fs_.weightSmooth = 0;
    Pose::Roi roi; float score = 0;
    const bool det = tr.detect(im, fs_, roi, score, err);
    // the face in the test picture: its middle is about (705, 215) of 1408 x 768, about 200 px wide
    check(det && std::fabs(roi.cx - 705) < 40 && std::fabs(roi.cy - 215) < 50 && roi.size > 200 && roi.size < 450, "BlazeFace finds the face",
          "score %.2f, at (%.0f, %.0f), %.0f px, %.1f deg", score, roi.cx, roi.cy, roi.size, roi.rot * 180 / M::PI);
    Face::Result r, r2;
    tr.process(im, nullptr, fs_, r, err);
    tr.process(im, nullptr, fs_, r2, err);   // the second frame tracks from the first's landmarks
    const float W = (float)im.w, H = (float)im.h;
    const float eyeR = r2.image[Face::R_EYE_OUT].x * W, eyeL = r2.image[Face::L_EYE_OUT].x * W;
    const float browY = r2.image[105].y * H, eyeY = r2.image[159].y * H, noseY = r2.image[Face::NOSE_TIP].y * H, mouthY = r2.image[Face::LIP_TOP].y * H, chinY = r2.image[Face::CHIN].y * H;
    check(r.found && r.detected && r2.found && !r2.detected && r2.score > 0.9f, "the face mesh: found, then tracked", "score %.3f, %.0f ms", r2.score, r2.ms);
    check(eyeR < eyeL && browY < eyeY && eyeY < noseY && noseY < mouthY && mouthY < chinY && eyeL - eyeR > 60 && eyeL - eyeR < 160,
          "the mesh is a face", "eyes %.0f..%.0f px, brow %.0f < eye %.0f < nose %.0f < mouth %.0f < chin %.0f", eyeR, eyeL, browY, eyeY, noseY, mouthY, chinY);
    const float irisR = r2.image[Face::R_IRIS].x * W, irisL = r2.image[Face::L_IRIS].x * W;
    check(std::min(irisR, irisL) > eyeR && std::max(irisR, irisL) < eyeL, "the irises are in the eyes", "%.0f, %.0f", irisR, irisL);
    const float smile = (r2.weights[44] + r2.weights[45]) / 2, blink = (r2.weights[9] + r2.weights[10]) / 2;
    check(smile > 0.3f && blink < 0.5f, "blendshapes: he smiles, his eyes are open", "smile %.2f, blink %.2f, jawOpen %.2f", smile, blink, r2.weights[25]);
    check(std::fabs(r2.yaw) < 20 && std::fabs(r2.pitch) < 25 && std::fabs(r2.roll) < 12, "the head faces the camera",
          "yaw %.1f, pitch %.1f, roll %.1f", r2.yaw, r2.pitch, r2.roll);

    // Sharpie markers: none on the bare face, then dots inked where Pantomimus says to draw them
    Markers::Settings ms;
    const std::vector<Markers::Blob> bare = Markers::findDots(im, r2, ms);
    std::string where;
    for (const auto& b : bare) where += " (" + std::to_string((int)b.x) + "," + std::to_string((int)b.y) + ")";
    check(bare.size() <= 3, "a bare face has (almost) no dots", "%zu%s", bare.size(), where.c_str());
    Image inked = im;
    const float rad = std::max(2.5f, (eyeL - eyeR) * 0.045f);
    for (int i = 0; i < Markers::COUNT; ++i) {
        float px, py; Markers::spotPosition(r2, i, im.w, im.h, px, py);
        inkDot(inked, px + 2, py - 1, rad);   // drawn a little off the exact spot, as by hand
    }
    Img::savePng(Util::join(Util::tempDir(), "pantomimus_face_inked.png"), inked);   // for `--script markers`
    Face::Tracker tr2; tr2.load(dir, 0, err);
    Face::Result rf;
    tr2.process(inked, nullptr, fs_, rf, err);
    Markers::Tracker mk;
    const int cal = mk.calibrate(inked, rf, ms);
    std::string missing;
    for (int i = 0; i < Markers::COUNT; ++i) if (!mk.have(i)) missing += std::string(missing.empty() ? " missing: " : ", ") + Markers::LAYOUT[i].name;
    {   // a picture to look at: where each dot should be (green) and every dot found (red), in the temp folder
        Image dbg = inked;
        auto plot = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b) {
            for (int k = -3; k <= 3; ++k) for (int j : {0}) {
                const int px = (int)x + k, py = (int)y + j, qx = (int)x + j, qy = (int)y + k;
                if (px >= 0 && py >= 0 && px < dbg.w && py < dbg.h) { uint8_t* p = &dbg.rgb[((size_t)py * dbg.w + px) * 3]; p[0] = r; p[1] = g; p[2] = b; }
                if (qx >= 0 && qy >= 0 && qx < dbg.w && qy < dbg.h) { uint8_t* p = &dbg.rgb[((size_t)qy * dbg.w + qx) * 3]; p[0] = r; p[1] = g; p[2] = b; }
            }
        };
        for (const auto& b : Markers::findDots(inked, rf, ms)) plot(b.x, b.y, 255, 0, 0);
        for (int i = 0; i < Markers::COUNT; ++i) { float px, py; Markers::spotPosition(rf, i, im.w, im.h, px, py); plot(px + 6, py, 0, 200, 0); }
        Img::savePng(Util::join(Util::tempDir(), "pantomimus_markers.png"), dbg);
    }
    check(cal >= Markers::COUNT - 3, "calibration finds the dots", "%d of %d%s", cal, Markers::COUNT, missing.c_str());
    // the same dots moved (the mouth's corners up and out 4 px, as in a smile): tracked, and they pull their landmarks
    Image moved = im;
    int cornerR = -1, cornerL = -1;   // LAYOUT's mouth corners
    for (int i = 0; i < Markers::COUNT; ++i) { if (std::string(Markers::LAYOUT[i].name) == "mouth corner (right)") cornerR = i; if (std::string(Markers::LAYOUT[i].name) == "mouth corner (left)") cornerL = i; }
    for (int i = 0; i < Markers::COUNT; ++i) {
        float px, py; Markers::spotPosition(r2, i, im.w, im.h, px, py);
        float dx = 2, dy = -1;
        if (i == cornerR) { dx -= 4; dy -= 4; }
        if (i == cornerL) { dx += 4; dy -= 4; }
        inkDot(moved, px + dx, py + dy, rad);
    }
    Face::Result rm;
    Markers::Result mr;
    Face::Tracker tr3; tr3.load(dir, 0, err);
    tr3.process(moved, nullptr, fs_, rm, err, [&](Face::Result& f) { mk.track(moved, f, ms, mr); });
    check(mr.found >= cal - 2, "the dots are tracked", "%d of %d", mr.found, cal);
    bool cornersSeen = mr.seen[cornerR] && mr.seen[cornerL];
    float errR = 1e9f, errL = 1e9f;
    if (cornersSeen) {
        float rx, ry, lx, ly;
        Markers::spotPosition(r2, cornerR, im.w, im.h, rx, ry);
        Markers::spotPosition(r2, cornerL, im.w, im.h, lx, ly);
        errR = std::hypot(mr.x[cornerR] * W - (rx - 2), mr.y[cornerR] * H - (ry - 5));
        errL = std::hypot(mr.x[cornerL] * W - (lx + 6), mr.y[cornerL] * H - (ly - 5));
    }
    check(cornersSeen && errR < 1.5f && errL < 1.5f, "the moved mouth corners are where they were inked", "off by %.2f and %.2f px", errR, errL);
}

int run(const std::string& testsDir) {
    App::out("Pantomimus " PANTOMIMUS_VERSION " self-test");
    maths();
    skeletonAndSolver(testsDir);
    filters();
    takesAndExports();
    tracking(testsDir);
    face(testsDir);
    App::out(fails ? "FAILED: " + std::to_string(fails) + " of " + std::to_string(fails + passes) : "All " + std::to_string(passes) + " passed");
    return fails;
}

}  // namespace SelfTest
