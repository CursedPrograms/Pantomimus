// Pantomimus - solving (see solver.h)
#include "solver.h"

#include <algorithm>
#include <cmath>

namespace Solver {

using M::Q;
using M::V3;
using namespace Pose;

static float smoothstep(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
static float angleBetween(const V3& a, const V3& b) { return std::acos(std::clamp(M::dot(M::normalize(a), M::normalize(b)), -1.0f, 1.0f)); }

// a bone's world turn: carried by its parent, swung so its rest direction d0 points along d, then twisted about d so
// its rest hint h0 lies as near the hint h as the weight says
static Q aim(const Q& parent, const V3& d0, const V3& d, const V3& h0, const V3& h, float twist) {
    const V3 dn = M::normalize(d);
    const V3 carried = M::rotate(parent, M::normalize(d0));
    Q r = M::normalize(M::fromTo(carried, dn) * parent);
    if (twist > 1e-3f) {
        const V3 cur = M::reject(M::rotate(r, h0), dn), want = M::reject(h, dn);
        if (M::length(cur) > 1e-5f && M::length(want) > 1e-5f) {
            const V3 a = M::normalize(cur), b = M::normalize(want);
            const float ang = std::atan2(M::dot(M::cross(a, b), dn), M::dot(a, b));
            r = M::normalize(M::axisAngle(dn, ang * std::min(twist, 1.0f)) * r);
        }
    }
    return r;
}

void Solver::reset() {
    havePrev_ = false; haveOrigin_ = false;
    locked_[0] = locked_[1] = false;
}

void Solver::prepare(const Skel::Skeleton& s) {
    if (skel_ == &s && skelBones_ == s.bones.size() && std::fabs(skelHeight_ - s.height()) < 1e-5f) return;
    skel_ = &s; skelBones_ = s.bones.size(); skelHeight_ = s.height();
    ro_ = Skel::roles(s);
    havePrev_ = false;
    for (V3& v : rest_) v = V3();
    auto head = [&](int b) { return b >= 0 ? s.bones[b].head : V3(); };
    auto tail = [&](int b) { return b >= 0 ? s.bones[b].tail : V3(); };
    for (int side = 0; side < 2; ++side) {
        const int o = side;   // landmarks: left even after the shoulders (11 L, 12 R)
        rest_[L_SHOULDER + o] = head(ro_.upperarm[side]);
        rest_[L_ELBOW + o] = head(ro_.lowerarm[side]);
        rest_[L_WRIST + o] = head(ro_.hand[side]);
        const int idx = ro_.finger[side][1][0], pky = ro_.finger[side][4][0], thb = ro_.finger[side][0][0];
        rest_[L_INDEX + o] = idx >= 0 ? head(idx) : tail(ro_.hand[side]);
        rest_[L_PINKY + o] = pky >= 0 ? head(pky) : tail(ro_.hand[side]);
        rest_[L_THUMB + o] = thb >= 0 ? head(thb) : tail(ro_.hand[side]);
        rest_[L_HIP + o] = head(ro_.thigh[side]);
        rest_[L_KNEE + o] = head(ro_.calf[side]);
        rest_[L_ANKLE + o] = head(ro_.foot[side]);
        rest_[L_FOOT + o] = ro_.ball[side] >= 0 ? head(ro_.ball[side]) : tail(ro_.foot[side]);
        rest_[L_HEEL + o] = head(ro_.foot[side]) - V3(0, head(ro_.foot[side]).y * 0.8f, 0.05f);
    }
    restFloor_ = 1e9f;
    for (int side = 0; side < 2; ++side)
        for (int b : {ro_.foot[side], ro_.ball[side]})
            if (b >= 0) restFloor_ = std::min({restFloor_, s.bones[b].head.y, s.bones[b].tail.y});
    if (restFloor_ > 1e8f) restFloor_ = 0;
}

V3 cameraPosition(const Result& r, float fovDeg) {
    // pinhole: depth = focal * world size / picture size, over the torso's sides (they don't foreshorten much)
    const float f = (r.frameW * 0.5f) / std::tan(std::clamp(fovDeg, 10.0f, 170.0f) * 0.5f * M::PI / 180);
    const int seg[][2] = {{L_SHOULDER, R_SHOULDER}, {L_HIP, R_HIP}, {L_SHOULDER, L_HIP}, {R_SHOULDER, R_HIP}};
    float sw = 0, si = 0;
    for (const auto& p : seg) {
        const Point& a = r.world[p[0]]; const Point& b = r.world[p[1]];
        sw += std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        const float dx = (r.image[p[0]].x - r.image[p[1]].x) * r.frameW, dy = (r.image[p[0]].y - r.image[p[1]].y) * r.frameH;
        si += std::sqrt(dx * dx + dy * dy);
    }
    if (si < 1e-3f) return V3();
    const float depth = f * sw / si;
    const float u = (r.image[L_HIP].x + r.image[R_HIP].x) * 0.5f * r.frameW, v = (r.image[L_HIP].y + r.image[R_HIP].y) * 0.5f * r.frameH;
    return {(u - r.frameW * 0.5f) / f * depth, (v - r.frameH * 0.5f) / f * depth, depth};
}

void Solver::solve(const Skel::Skeleton& s, const Result& r, double t, const Settings& set, Frame& out) {
    prepare(s);
    Skel::Pose& p = out.pose;
    p.reset(s);
    if (!r.found) {
        if (havePrev_) { p = prev_; }
        out.contact[0] = out.contact[1] = false;
        return;
    }
    // the landmarks in the skeleton's space: Y up, facing +Z; mirrored: x flipped and left and right swapped
    V3 P[N];
    float vis[N];
    for (int i = 0; i < N; ++i) {
        int src = i;
        if (set.mirror && i > 0) {
            // left/right pairs: 1-3 <-> 4-6, then odd/even from 7
            if (i >= 1 && i <= 3) src = i + 3;
            else if (i >= 4 && i <= 6) src = i - 3;
            else src = (i % 2 == 1) ? i + 1 : i - 1;
        }
        const Point& w = r.world[src];
        P[i] = V3(set.mirror ? -w.x : w.x, -w.y, -w.z);
        vis[i] = r.visibility[src];
    }
    const size_t nb = s.bones.size();
    std::vector<Q> W(nb);
    // the previous frame's world turns, for unseen limbs held
    Skel::World prevW;
    if (havePrev_) Skel::forward(s, prev_, prevW);
    for (size_t i = 0; i < nb; ++i) W[i] = s.bones[i].parent < 0 ? Q() : W[s.bones[i].parent];   // followed by default

    auto setTree = [&](int b, const Q& q) {   // a bone and everything under it carried
        if (b < 0) return;
        const Q delta = M::normalize(q * M::conj(W[b]));
        for (size_t i = b; i < nb; ++i) {
            if ((int)i == b) { W[i] = q; continue; }
            int a = s.bones[i].parent; bool under = false;
            while (a >= 0) { if (a == b) { under = true; break; } a = s.bones[a].parent; }
            if (under) W[i] = M::normalize(delta * W[i]);
        }
    };

    // the torso: hips and chest frames
    const V3 midHip = (P[L_HIP] + P[R_HIP]) * 0.5f, midSh = (P[L_SHOULDER] + P[R_SHOULDER]) * 0.5f;
    const V3 restMidHip = (rest_[L_HIP] + rest_[R_HIP]) * 0.5f, restMidSh = (rest_[L_SHOULDER] + rest_[R_SHOULDER]) * 0.5f;
    const V3 up = midSh - midHip, restUp = restMidSh - restMidHip;
    Q pelvisW, chestW;
    if (set.torso) {
        pelvisW = M::normalize(M::frameXY(P[L_HIP] - P[R_HIP], up) * M::conj(M::frameXY(rest_[L_HIP] - rest_[R_HIP], restUp)));
        chestW = M::normalize(M::frameXY(P[L_SHOULDER] - P[R_SHOULDER], up) * M::conj(M::frameXY(rest_[L_SHOULDER] - rest_[R_SHOULDER], restUp)));
    } else {
        // the torso still: only the body's facing (about Y) from the hips
        V3 x = P[L_HIP] - P[R_HIP]; x.y = 0;
        pelvisW = chestW = M::fromTo(V3(1, 0, 0), M::normalize(x, V3(1, 0, 0)));
    }
    setTree(ro_.pelvis, pelvisW);
    // the spine shares the chest's turn over the hips
    const Q rel = M::normalize(M::conj(pelvisW) * chestW);
    const float sb = std::clamp(set.spineBend, 0.0f, 1.0f);
    for (int k = 0; k < 3; ++k)
        if (ro_.spine[k] >= 0) setTree(ro_.spine[k], M::normalize(pelvisW * M::slerp(Q(), rel, sb * (k + 1) / 3.0f + (1 - sb) * (k == 2 ? 1.0f : 0.0f))));
    const Q chest = ro_.spine[2] >= 0 ? W[ro_.spine[2]] : chestW;

    // the head from the ears and the nose; the neck takes half
    if (set.head && ro_.head >= 0) {
        const float hv = std::min({vis[L_EAR], vis[R_EAR], vis[NOSE]});
        if (hv >= set.visibility * 0.5f) {
            const V3 earMid = (P[L_EAR] + P[R_EAR]) * 0.5f, x = P[L_EAR] - P[R_EAR], fwd = P[NOSE] - earMid;
            const float pr = set.headPitch * M::PI / 180;
            const V3 restFwd(0, -std::sin(pr), std::cos(pr));
            const Q headW = M::normalize(M::frameXY(x, M::cross(fwd, x)) * M::conj(M::frameXY(V3(1, 0, 0), M::cross(restFwd, V3(1, 0, 0)))));
            if (ro_.neck >= 0) setTree(ro_.neck, M::slerp(chest, headW, 0.5f));
            setTree(ro_.head, headW);
        }
    }

    auto seen = [&](std::initializer_list<int> ls) { float m = 1; for (int l : ls) m = std::min(m, vis[l]); return m >= set.visibility; };
    auto hold = [&](int b) {   // an unseen limb: as at rest (carried) or as last seen
        if (b < 0) return;
        if (set.unseen == UNSEEN_HOLD && havePrev_) setTree(b, M::normalize(W[s.bones[b].parent] * prev_.local[b]));
    };

    // the arms
    for (int side = 0; side < 2; ++side) {
        const int o = side;
        const int cl = ro_.clavicle[side], ua = ro_.upperarm[side], la = ro_.lowerarm[side], hd = ro_.hand[side];
        if (cl >= 0 && set.torso && set.shrug > 0) {
            // the shoulder raised or dropped: the clavicle turns towards where the shoulder is
            const float k = M::length(P[L_SHOULDER] - P[R_SHOULDER]) / std::max(1e-4f, M::length(rest_[L_SHOULDER] - rest_[R_SHOULDER]));
            const V3 clHead = midSh + M::rotate(chest, s.bones[cl].head - restMidSh) * k;
            const Q full = aim(W[s.bones[cl].parent], s.bones[cl].tail - s.bones[cl].head, P[L_SHOULDER + o] - clHead, V3(), V3(), 0);
            setTree(cl, M::slerp(W[cl], full, std::clamp(set.shrug, 0.0f, 1.0f)));
        }
        const bool armSeen = set.arms && seen({L_SHOULDER + o, L_ELBOW + o, L_WRIST + o});
        out.limbSeen[side] = armSeen;
        if (!armSeen) { hold(ua); continue; }
        if (ua >= 0 && la >= 0) {
            const V3 d0 = rest_[L_ELBOW + o] - rest_[L_SHOULDER + o], d = P[L_ELBOW + o] - P[L_SHOULDER + o];
            const V3 l0 = rest_[L_WRIST + o] - rest_[L_ELBOW + o], l = P[L_WRIST + o] - P[L_ELBOW + o];
            const float bendRest = angleBetween(d0, l0), bend = angleBetween(d, l);
            const float tw = smoothstep(0.17f, 0.6f, bend) * smoothstep(0.17f, 0.6f, bendRest);
            setTree(ua, aim(W[s.bones[ua].parent], d0, d, M::reject(l0, M::normalize(d0)), M::reject(l, M::normalize(d)), tw));
            // the forearm: towards the wrist, twisted by the palm (index to pinky)
            const V3 a0 = rest_[L_INDEX + o] - rest_[L_PINKY + o], a = P[L_INDEX + o] - P[L_PINKY + o];
            const float handVis = std::min(vis[L_INDEX + o], vis[L_PINKY + o]) >= set.visibility ? 1.0f : 0.0f;
            setTree(la, aim(W[ua], l0, l, a0, a, set.forearmTwist * handVis));
            if (hd >= 0 && handVis > 0) {
                const V3 h0 = (rest_[L_INDEX + o] + rest_[L_PINKY + o]) * 0.5f - rest_[L_WRIST + o];
                const V3 h = (P[L_INDEX + o] + P[L_PINKY + o]) * 0.5f - P[L_WRIST + o];
                setTree(hd, aim(W[la], h0, h, a0, a, 1));
            }
        }
    }
    // the legs
    for (int side = 0; side < 2; ++side) {
        const int o = side;
        const int th = ro_.thigh[side], ca = ro_.calf[side], ft = ro_.foot[side];
        const bool legSeen = set.legs && seen({L_HIP + o, L_KNEE + o, L_ANKLE + o});
        out.limbSeen[2 + side] = legSeen;
        if (!legSeen) { hold(th); continue; }
        if (th < 0 || ca < 0) continue;
        const V3 d0 = rest_[L_KNEE + o] - rest_[L_HIP + o], d = P[L_KNEE + o] - P[L_HIP + o];
        const V3 c0 = rest_[L_ANKLE + o] - rest_[L_KNEE + o], c = P[L_ANKLE + o] - P[L_KNEE + o];
        const V3 f0 = rest_[L_FOOT + o] - rest_[L_ANKLE + o], f = P[L_FOOT + o] - P[L_ANKLE + o];
        const bool footSeen = vis[L_FOOT + o] >= set.visibility;
        // the thigh's twist: where the knee points (the bend), or where the foot points when the leg is straight
        auto kneeHint = [](const V3& thigh, const V3& calf, const V3& foot) {
            const V3 tn = M::normalize(thigh);
            const V3 knee = -M::reject(calf, tn), toe = M::reject(foot, tn);
            const float wb = smoothstep(0.15f, 0.5f, angleBetween(thigh, calf));
            return M::normalize(knee, V3(0, 0, 1)) * wb + M::normalize(toe, V3(0, 0, 1)) * (1 - wb);
        };
        setTree(th, aim(W[s.bones[th].parent], d0, d, kneeHint(d0, c0, f0), kneeHint(d, c, footSeen ? f : M::rotate(W[th], f0)), 1));
        setTree(ca, aim(W[th], c0, c, f0, f, footSeen ? 0.5f : 0));
        if (ft >= 0 && footSeen) setTree(ft, aim(W[ca], f0, f, V3(), V3(), 0));
    }
    Skel::localFromWorld(s, W, p);
    for (size_t i = 0; i < nb; ++i) if (havePrev_ && i < prev_.local.size()) p.local[i] = M::sameHemisphere(p.local[i], prev_.local[i]);

    // where the body is
    const float dt = havePrev_ ? (float)std::clamp(t - lastT_, 1e-3, 0.5) : 1.0f / 30;
    V3 rootXZ;
    if (set.rootMode != ROOT_IN_PLACE || set.rootMode == ROOT_FULL) {
        V3 cam = cameraPosition(r, set.fov);
        V3 sk(set.mirror ? -cam.x : cam.x, -cam.y, -cam.z);   // camera to skeleton space (towards the camera: +Z)
        if (!haveOrigin_) { origin_ = sk; haveOrigin_ = true; }
        rootXZ = (sk - origin_) * set.rootScale;
    }
    p.root = s.bones[0].head;
    if (set.rootMode != ROOT_IN_PLACE) { p.root.x += rootXZ.x; p.root.z += rootXZ.z; }
    p.hips = V3();
    if (set.rootMode == ROOT_FULL) p.hips.y = rootXZ.y;
    Skel::World w;
    Skel::forward(s, p, w);
    auto lowest = [&](int side) {
        float lo = 1e9f;
        for (int b : {ro_.foot[side], ro_.ball[side]}) if (b >= 0) lo = std::min({lo, w.head[b].y, w.tail[b].y});
        return lo;
    };
    if (set.groundLock) {
        const float lo = std::min(lowest(0), lowest(1));
        if (lo < 1e8f) {
            float dy = restFloor_ - lo;
            if (set.rootMode == ROOT_FULL) dy = std::max(0.0f, dy);   // jumps stay; only the floor holds the feet
            p.hips.y += dy;
            Skel::forward(s, p, w);
        }
    }
    // planted feet: near the floor and still; a planted foot holds its place and the root moves instead
    for (int side = 0; side < 2; ++side) {
        const int fb = ro_.ball[side] >= 0 ? ro_.ball[side] : ro_.foot[side];
        if (fb < 0) continue;
        const V3 at = (w.head[ro_.foot[side]] + w.head[fb]) * 0.5f;
        const float speed = havePrev_ ? M::length(V3(at.x - lastFoot_[side].x, 0, at.z - lastFoot_[side].z)) / dt : 0;
        const bool planted = lowest(side) - restFloor_ < set.contactHeight && speed < set.contactSpeed;
        out.contact[side] = planted;
        if (planted && !locked_[side]) { lockAt_[side] = at; locked_[side] = true; }
        if (!planted) locked_[side] = false;
        lastFoot_[side] = at;
    }
    if (set.footLock && set.rootMode != ROOT_IN_PLACE) {
        V3 shift; int n = 0;
        for (int side = 0; side < 2; ++side)
            if (locked_[side]) {
                const int fb = ro_.ball[side] >= 0 ? ro_.ball[side] : ro_.foot[side];
                const V3 at = (w.head[ro_.foot[side]] + w.head[fb]) * 0.5f;
                shift += V3(lockAt_[side].x - at.x, 0, lockAt_[side].z - at.z); ++n;
            }
        if (n) {
            shift = shift / (float)n;
            p.root += shift;
            origin_ -= V3(shift.x, 0, shift.z) / std::max(1e-3f, set.rootScale);   // the drift is taken out of the estimate for good
            for (int side = 0; side < 2; ++side) lastFoot_[side] += shift;
        }
    }
    prev_ = p; havePrev_ = true; lastT_ = t;
}

void twoBoneIK(const Skel::Skeleton& s, Skel::Pose& p, int upper, int lower, const V3& target, const V3& pole) {
    if (upper < 0 || lower < 0) return;
    Skel::World w;
    Skel::forward(s, p, w);
    // the chain's end: the lower bone's tail, or its child's head
    const V3 a = w.head[upper], b = w.head[lower], c = w.tail[lower];
    const float la = M::length(b - a), lb = M::length(c - b);
    V3 toT = target - a;
    const float dist = std::clamp(M::length(toT), std::fabs(la - lb) + 1e-4f, la + lb - 1e-4f);
    toT = M::normalize(toT) * dist;
    // the knee's angle by the law of cosines, in the plane of the chain and the pole
    const float cosA = std::clamp((la * la + dist * dist - lb * lb) / (2 * la * dist), -1.0f, 1.0f);
    const V3 dirT = M::normalize(toT);
    V3 bend = M::reject(pole - a, dirT);
    if (M::length(bend) < 1e-5f) bend = M::reject(b - a, dirT);
    bend = M::normalize(bend, V3(0, 0, 1));
    const V3 kneeAt = a + (dirT * cosA + bend * std::sqrt(std::max(0.0f, 1 - cosA * cosA))) * la;
    // turn the upper bone to the new knee, then the lower one to the target
    std::vector<Q> W = w.rot;
    const int up = s.bones[upper].parent;
    const Q upNew = M::normalize(M::fromTo(M::normalize(b - a), M::normalize(kneeAt - a)) * W[upper]);
    const Q dUp = M::normalize(upNew * M::conj(W[upper]));
    for (size_t i = upper; i < W.size(); ++i) {
        int q = (int)i; bool under = false;
        while (q >= 0) { if (q == upper) { under = true; break; } q = s.bones[q].parent; }
        if (under) W[i] = M::normalize(dUp * W[i]);
    }
    const V3 lowDir = M::rotate(W[lower], s.bones[lower].tail - s.bones[lower].head);
    const Q dLo = M::normalize(M::fromTo(M::normalize(lowDir), M::normalize(target - kneeAt)));
    for (size_t i = lower; i < W.size(); ++i) {
        int q = (int)i; bool under = false;
        while (q >= 0) { if (q == lower) { under = true; break; } q = s.bones[q].parent; }
        if (under) W[i] = M::normalize(dLo * W[i]);
    }
    (void)up;
    // the bones under the lower one keep their world turn (the foot stays flat)
    for (size_t i = lower + 1; i < W.size(); ++i) {
        int q = s.bones[i].parent; bool under = false;
        while (q >= 0) { if (q == lower) { under = true; break; } q = s.bones[q].parent; }
        if (under) W[i] = w.rot[i];
    }
    Skel::localFromWorld(s, W, p);
}

}  // namespace Solver
