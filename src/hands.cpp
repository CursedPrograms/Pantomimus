// Pantomimus - the hands (see hands.h)
#include "hands.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace Hands {

using M::Q;
using M::V3;

const int BONES[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 5}, {5, 6}, {6, 7}, {7, 8}, {5, 9}, {9, 10}, {10, 11}, {11, 12},
                        {9, 13}, {13, 14}, {14, 15}, {15, 16}, {13, 17}, {0, 17}, {17, 18}, {18, 19}, {19, 20}};
const int BONE_COUNT = sizeof(BONES) / sizeof(BONES[0]);

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-std::clamp(x, -80.0f, 80.0f))); }

bool Tracker::load(const std::string& dir, int threads, std::string& err) {
    if (lm_.loaded() && fs::u8path(lm_.path()).parent_path() == fs::u8path(dir)) return true;
    return lm_.load((fs::u8path(dir) / LANDMARK_FILE).u8string(), threads, err);
}

bool Tracker::landmarks(const Image& frame, const Pose::Roi& roi, Hand& h, std::string& err) {
    const int n = 224;
    buf_.resize((size_t)n * n * 3);
    Img::cropRotated(frame, roi.cx, roi.cy, roi.size, roi.size, roi.rot, n, 0, 1, buf_.data());
    std::vector<std::vector<float>> o;
    if (!lm_.run(buf_.data(), {1, n, n, 3}, o, err)) return false;
    if (o.size() < 4) { err = "The hand model's outputs aren't MediaPipe Hands'"; return false; }
    // Identity: 21 x 3 in the crop's pixels; Identity_1: a hand is here; Identity_2: left or right; Identity_3: world
    const std::vector<float>& lm = o[0];
    const std::vector<float>& world = o[3];
    if (lm.size() < 63 || world.size() < 63) { err = "The hand model's landmarks aren't 21 x 3"; return false; }
    h.score = o[1].empty() ? 0 : o[1][0];
    if (h.score < 0 || h.score > 1) h.score = sigmoid(h.score);
    const float c = std::cos(roi.rot), s = std::sin(roi.rot);
    for (int i = 0; i < N; ++i) {
        const float u = (lm[i * 3] / n - 0.5f) * roi.size, v = (lm[i * 3 + 1] / n - 0.5f) * roi.size;
        h.image[i].x = (roi.cx + u * c - v * s) / frame.w;
        h.image[i].y = (roi.cy + u * s + v * c) / frame.h;
        h.image[i].z = lm[i * 3 + 2] / n * roi.size / frame.w;
        const float wx = world[i * 3], wy = world[i * 3 + 1];
        h.world[i] = {c * wx - s * wy, s * wx + c * wy, world[i * 3 + 2]};
    }
    h.roi = roi;
    return true;
}

bool Tracker::process(const Image& frame, const Pose::Result& body, const Settings& s, Result& out, std::string& err) {
    out = Result();
    if (!body.found || !lm_.loaded()) return true;
    for (int side = 0; side < 2; ++side) {
        // MediaPipe's hand region from the body: the middle finger guessed from the index and pinky, the box twice
        // its distance from the wrist, turned so the fingers point up
        const Pose::Point& w = body.image[Pose::L_WRIST + side];
        const Pose::Point& ix = body.image[Pose::L_INDEX + side];
        const Pose::Point& pk = body.image[Pose::L_PINKY + side];
        if (std::min({body.visibility[Pose::L_WRIST + side], body.visibility[Pose::L_INDEX + side], body.visibility[Pose::L_PINKY + side]}) < 0.3f) continue;
        const float wx = w.x * frame.w, wy = w.y * frame.h;
        const float mx = (2 * ix.x + pk.x) / 3 * frame.w, my = (2 * ix.y + pk.y) / 3 * frame.h;
        Pose::Roi roi;
        const float size = 2 * std::hypot(mx - wx, my - wy);
        roi.rot = 3.14159265f / 2 - std::atan2(-(my - wy), mx - wx);
        roi.rot = roi.rot - 2 * 3.14159265f * std::floor((roi.rot + 3.14159265f) / (2 * 3.14159265f));
        roi.size = size * s.roiScale;
        // shifted a tenth of the box towards the fingers (MediaPipe: shift_y -0.1)
        const float shift = -0.1f * roi.size;
        roi.cx = mx - shift * std::sin(roi.rot);
        roi.cy = my + shift * std::cos(roi.rot);
        roi.valid = roi.size > 8;
        if (!roi.valid) continue;
        Hand h;
        if (!landmarks(frame, roi, h, err)) return false;
        h.found = h.score >= s.presence;
        if (h.found && prev_.hand[side].found && s.smooth > 0) {
            const float k = std::clamp(s.smooth, 0.0f, 0.95f);
            for (int i = 0; i < N; ++i) {
                Pose::Point& a = h.world[i]; const Pose::Point& b = prev_.hand[side].world[i];
                a.x = a.x * (1 - k) + b.x * k; a.y = a.y * (1 - k) + b.y * k; a.z = a.z * (1 - k) + b.z * k;
            }
        }
        out.hand[side] = h;
    }
    prev_ = out;
    return true;
}

void solveFingers(const Skel::Skeleton& s, const Result& r, bool mirror, const Settings& set, Skel::Pose& p) {
    const Skel::Roles ro = Skel::roles(s);
    Skel::World w;
    Skel::forward(s, p, w);
    std::vector<Q> W = w.rot;
    for (int side = 0; side < 2; ++side) {
        const Hand& h = r.hand[mirror ? 1 - side : side];
        const int hb = ro.hand[side];
        if (!h.found || hb < 0) continue;
        V3 P[N];
        for (int i = 0; i < N; ++i) P[i] = V3(mirror ? -h.world[i].x : h.world[i].x, -h.world[i].y, -h.world[i].z);
        // the palm's frame seen, and at rest (the hand bone's head, the index's and pinky's first knuckles)
        const int idx = ro.finger[side][1][0], pky = ro.finger[side][4][0];
        if (idx < 0 || pky < 0) continue;
        const V3 restWrist = s.bones[hb].head, restIdx = s.bones[idx].head, restPky = s.bones[pky].head;
        auto frame = [](const V3& wr, const V3& a, const V3& b) {
            const V3 fwd = (a + b) * 0.5f - wr, across = a - b;
            return M::frameYZ(fwd, M::cross(across, fwd));
        };
        const Q seen = frame(P[0], P[5], P[17]), rest = frame(restWrist, restIdx, restPky);
        // the palm seen -> the palm at rest: finger directions read in the hand's own frame, then put on the skeleton's hand
        const Q toRest = M::normalize(rest * M::conj(seen));
        const int firstLm[5] = {1, 5, 9, 13, 17};
        for (int f = 0; f < 5; ++f)
            for (int k = 0; k < 3; ++k) {
                const int b = ro.finger[side][f][k];
                if (b < 0) continue;
                const V3 d = M::rotate(toRest, P[firstLm[f] + k + 1] - P[firstLm[f] + k]);
                const V3 target = M::rotate(W[hb], d);   // the hand at rest -> as the hand is now
                const int parent = s.bones[b].parent;
                const V3 d0 = s.bones[b].tail - s.bones[b].head;
                const Q carried = W[parent];
                const Q full = M::normalize(M::fromTo(M::normalize(M::rotate(carried, d0)), M::normalize(target)) * carried);
                // curl and spread: how much of the turn goes on
                const float amount = std::clamp(k == 0 ? (set.curl + set.spread) * 0.5f : set.curl, 0.0f, 1.0f);
                const Q q = M::slerp(carried, full, amount);
                const Q delta = M::normalize(q * M::conj(W[b]));
                for (size_t i = b; i < W.size(); ++i) {
                    int a = (int)i; bool under = false;
                    while (a >= 0) { if (a == b) { under = true; break; } a = s.bones[a].parent; }
                    if (under) W[i] = M::normalize(delta * W[i]);
                }
            }
    }
    Skel::localFromWorld(s, W, p);
}

}  // namespace Hands
