// Pantomimus - the face (see face.h)
#include "face.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace Face {

using M::Q;
using M::V3;

const char* const BLENDSHAPE_NAMES[BLENDSHAPES] = {
    "_neutral", "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff",
    "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
    "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight",
    "eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen", "jawRight",
    "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight", "mouthFunnel", "mouthLeft",
    "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight",
    "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight",
    "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight"};

// MediaPipe's face_blendshapes_graph.cc kLandmarksSubsetIdxs
const int SUBSET_INDEX[SUBSET] = {
    0, 1, 4, 5, 6, 7, 8, 10, 13, 14, 17, 21, 33, 37, 39, 40, 46, 52, 53, 54, 55, 58, 61, 63, 65, 66, 67, 70, 78, 80, 81, 82,
    84, 87, 88, 91, 93, 95, 103, 105, 107, 109, 127, 132, 133, 136, 144, 145, 146, 148, 149, 150, 152, 153, 154, 155, 157,
    158, 159, 160, 161, 162, 163, 168, 172, 173, 176, 178, 181, 185, 191, 195, 197, 234, 246, 249, 251, 263, 267, 269, 270,
    276, 282, 283, 284, 285, 288, 291, 293, 295, 296, 297, 300, 308, 310, 311, 312, 314, 317, 318, 321, 323, 324, 332, 334,
    336, 338, 356, 361, 362, 365, 373, 374, 375, 377, 378, 379, 380, 381, 382, 384, 385, 386, 387, 388, 389, 390, 397, 398,
    400, 402, 405, 409, 415, 454, 466, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477};

// MediaPipe's face mesh connections, thinned to what reads as a face: the oval, the brows, the eyes, the lips, the nose
static const int OVAL[] = {10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148,
                           176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109, 10};
static const int BROW_R[] = {46, 53, 52, 65, 55, 107, 66, 105, 63, 70, 46};
static const int BROW_L[] = {276, 283, 282, 295, 285, 336, 296, 334, 293, 300, 276};
static const int EYE_R[] = {33, 246, 161, 160, 159, 158, 157, 173, 133, 155, 154, 153, 145, 144, 163, 7, 33};
static const int EYE_L[] = {263, 466, 388, 387, 386, 385, 384, 398, 362, 382, 381, 380, 374, 373, 390, 249, 263};
static const int LIPS_OUT[] = {61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146, 61};
static const int LIPS_IN[] = {78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95, 78};
static const int NOSE[] = {168, 6, 197, 195, 5, 4, 1, 19, 94, 2};

static std::vector<std::pair<int, int>> buildContour() {
    std::vector<std::pair<int, int>> c;
    auto add = [&](const int* p, size_t n) { for (size_t i = 0; i + 1 < n; ++i) c.push_back({p[i], p[i + 1]}); };
    add(OVAL, std::size(OVAL)); add(BROW_R, std::size(BROW_R)); add(BROW_L, std::size(BROW_L)); add(EYE_R, std::size(EYE_R));
    add(EYE_L, std::size(EYE_L)); add(LIPS_OUT, std::size(LIPS_OUT)); add(LIPS_IN, std::size(LIPS_IN)); add(NOSE, std::size(NOSE));
    return c;
}
static const std::vector<std::pair<int, int>> CONTOUR_V = buildContour();

int blendshapeIndex(const std::string& name) {
    for (int i = 0; i < BLENDSHAPES; ++i) if (Util::lower(name) == Util::lower(BLENDSHAPE_NAMES[i])) return i;
    return -1;
}

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-std::clamp(x, -80.0f, 80.0f))); }
static float wrapAngle(float a) { return a - 2 * M::PI * std::floor((a + M::PI) / (2 * M::PI)); }

std::vector<float> anchors() {
    // MediaPipe's SsdAnchorsCalculator for face_detection_short_range: 4 layers, strides 8 16 16 16, 128 x 128,
    // two anchors a layer, fixed size; layers with the same stride share their cells -> 16x16x2 + 8x8x6 = 896
    const int strides[4] = {8, 16, 16, 16};
    std::vector<float> a;
    int layer = 0;
    while (layer < 4) {
        int per = 0, last = layer;
        while (last < 4 && strides[last] == strides[layer]) { per += 2; ++last; }
        const int fm = (int)std::ceil(128.0f / strides[layer]);
        for (int y = 0; y < fm; ++y)
            for (int x = 0; x < fm; ++x)
                for (int k = 0; k < per; ++k) { a.push_back((x + 0.5f) / fm); a.push_back((y + 0.5f) / fm); a.push_back(1); a.push_back(1); }
        layer = last;
    }
    return a;
}

bool Tracker::load(const std::string& dir, int threads, std::string& err) {
    const std::string d = fs::u8path(dir).u8string();
    if (loaded() && dir_ == d) return true;
    if (!det_.load((fs::u8path(dir) / DETECTOR_FILE).u8string(), threads, err)) return false;
    if (!lm_.load((fs::u8path(dir) / LANDMARK_FILE).u8string(), threads, err)) return false;
    if (!bs_.load((fs::u8path(dir) / BLENDSHAPE_FILE).u8string(), threads, err)) return false;
    if (anchors_.empty()) anchors_ = anchors();
    dir_ = d;
    reset();
    return true;
}

bool Tracker::detect(const Image& frame, const Settings& s, Pose::Roi& roi, float& best, std::string& err) {
    const int n = 128;
    if (anchors_.empty()) anchors_ = anchors();
    buf_.resize((size_t)n * n * 3);
    const Img::Letterbox L = Img::letterbox(frame, n, -1, 1, buf_.data());
    std::vector<std::vector<float>> out;
    if (!det_.run(buf_.data(), {1, n, n, 3}, out, err)) return false;
    if (out.size() < 2) { err = "The face detector gave too few outputs"; return false; }
    const std::vector<float>& box = out[0].size() >= out[1].size() ? out[0] : out[1];
    const std::vector<float>& sc = out[0].size() >= out[1].size() ? out[1] : out[0];
    const size_t na = std::min(sc.size(), anchors_.size() / 4);
    if (box.size() < na * 16) { err = "The face detector's boxes aren't 16 numbers each"; return false; }
    int bi = -1; best = 0;
    for (size_t i = 0; i < na; ++i) { const float p = sigmoid(sc[i]); if (p > best) { best = p; bi = (int)i; } }
    if (bi < 0 || best < s.detectScore) return false;
    // cx cy w h, then 6 keypoints (right eye, left eye, nose, mouth, right ear, left ear), 0..1 of the 128 square
    auto decode = [&](size_t i, float* o) {
        const float* b = &box[i * 16];
        const float* a = &anchors_[i * 4];
        o[0] = b[0] / n * a[2] + a[0]; o[1] = b[1] / n * a[3] + a[1]; o[2] = b[2] / n * a[2]; o[3] = b[3] / n * a[3];
        for (int k = 0; k < 6; ++k) { o[4 + k * 2] = b[4 + k * 2] / n * a[2] + a[0]; o[5 + k * 2] = b[5 + k * 2] / n * a[3] + a[1]; }
    };
    float ref[16]; decode(bi, ref);
    auto iou = [](const float* p, const float* q) {
        const float ax0 = p[0] - p[2] / 2, ax1 = p[0] + p[2] / 2, ay0 = p[1] - p[3] / 2, ay1 = p[1] + p[3] / 2;
        const float bx0 = q[0] - q[2] / 2, bx1 = q[0] + q[2] / 2, by0 = q[1] - q[3] / 2, by1 = q[1] + q[3] / 2;
        const float iw = std::max(0.0f, std::min(ax1, bx1) - std::max(ax0, bx0)), ih = std::max(0.0f, std::min(ay1, by1) - std::max(ay0, by0));
        const float in = iw * ih, un = p[2] * p[3] + q[2] * q[3] - in;
        return un > 0 ? in / un : 0.0f;
    };
    // MediaPipe's weighted non-maximum suppression: the boxes overlapping the best, averaged by score
    float acc[16] = {}, wsum = 0;
    for (size_t i = 0; i < na; ++i) {
        const float p = sigmoid(sc[i]);
        if (p < s.detectScore) continue;
        float d[16]; decode(i, d);
        if (iou(ref, d) < 0.3f) continue;
        for (int k = 0; k < 16; ++k) acc[k] += d[k] * p;
        wsum += p;
    }
    if (wsum <= 0) return false;
    for (float& v : acc) v /= wsum;
    auto px = [&](float v) { return (v * n - L.padX) / L.scale; };
    auto py = [&](float v) { return (v * n - L.padY) / L.scale; };
    const float cx = px(acc[0]), cy = py(acc[1]), w = acc[2] * n / L.scale, h = acc[3] * n / L.scale;
    const float rx = px(acc[4]), ry = py(acc[5]), lx = px(acc[6]), ly = py(acc[7]);
    // MediaPipe's face_detection_short_range_detection_to_roi: turned so the eyes are level, square, 1.5 times
    roi.cx = cx; roi.cy = cy;
    roi.size = std::max(w, h) * s.roiScale;
    roi.rot = wrapAngle(-std::atan2(-(ly - ry), lx - rx));
    roi.valid = roi.size > 8;
    return roi.valid;
}

bool Tracker::landmarks(const Image& frame, const Pose::Roi& roi, Result& r, std::string& err) {
    const int n = 256;
    buf_.resize((size_t)n * n * 3);
    Img::cropRotated(frame, roi.cx, roi.cy, roi.size, roi.size, roi.rot, n, 0, 1, buf_.data());
    std::vector<std::vector<float>> o;
    if (!lm_.run(buf_.data(), {1, n, n, 3}, o, err)) return false;
    // the landmarks (1434 numbers), the face's presence (a logit) and the tongue's
    int li = -1, pi = -1;
    for (size_t i = 0; i < o.size(); ++i) if (o[i].size() >= (size_t)N * 3) li = (int)i;
    for (size_t i = 0; i < o.size() && i < lm_.outputNames().size(); ++i)
        if ((int)i != li && o[i].size() == 1 && lm_.outputNames()[i].size() >= 2 && lm_.outputNames()[i].substr(lm_.outputNames()[i].size() - 2) == "_1") pi = (int)i;
    if (pi < 0) for (size_t i = 0; i < o.size(); ++i) if ((int)i != li && o[i].size() == 1) { pi = (int)i; break; }
    if (li < 0) { err = "The face model's landmarks aren't 478 x 3"; return false; }
    const std::vector<float>& lm = o[li];
    r.score = pi >= 0 ? sigmoid(o[pi][0]) : 1.0f;
    const float c = std::cos(roi.rot), s = std::sin(roi.rot);
    for (int i = 0; i < N; ++i) {
        const float u = (lm[i * 3] / n - 0.5f) * roi.size, v = (lm[i * 3 + 1] / n - 0.5f) * roi.size;
        r.image[i].x = (roi.cx + u * c - v * s) / frame.w;
        r.image[i].y = (roi.cy + u * s + v * c) / frame.h;
        r.image[i].z = lm[i * 3 + 2] / n * roi.size / frame.w;
    }
    r.roi = roi;
    return true;
}

bool Tracker::blendshapes(const Result& r, int fw, int fh, float out[BLENDSHAPES], std::string& err) {
    float in[SUBSET * 2];
    for (int i = 0; i < SUBSET; ++i) { in[i * 2] = r.image[SUBSET_INDEX[i]].x * fw; in[i * 2 + 1] = r.image[SUBSET_INDEX[i]].y * fh; }
    std::vector<std::vector<float>> o;
    if (!bs_.run(in, {1, SUBSET, 2}, o, err)) return false;
    if (o.empty() || o[0].size() < (size_t)BLENDSHAPES) { err = "The blendshape model's outputs aren't 52"; return false; }
    for (int i = 0; i < BLENDSHAPES; ++i) out[i] = std::clamp(o[0][i], 0.0f, 1.0f);
    return true;
}

Pose::Roi roiFromLandmarks(const Result& r, int fw, int fh, float scale) {
    // MediaPipe's face_landmarks_to_roi: the landmarks' box, turned so 33 -> 263 (the eyes' outer corners) is level,
    // square on its long side, 1.5 times
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 468; ++i) {
        x0 = std::min(x0, r.image[i].x * fw); x1 = std::max(x1, r.image[i].x * fw);
        y0 = std::min(y0, r.image[i].y * fh); y1 = std::max(y1, r.image[i].y * fh);
    }
    Pose::Roi roi;
    roi.cx = (x0 + x1) / 2; roi.cy = (y0 + y1) / 2;
    roi.size = std::max(x1 - x0, y1 - y0) * scale;
    const float ax = r.image[R_EYE_OUT].x * fw, ay = r.image[R_EYE_OUT].y * fh, bx = r.image[L_EYE_OUT].x * fw, by = r.image[L_EYE_OUT].y * fh;
    roi.rot = wrapAngle(-std::atan2(-(by - ay), bx - ax));
    roi.valid = roi.size > 8;
    return roi;
}

// the face's region from the body's face points (nose, eyes, ears, mouth), when the body is tracked
static bool roiFromBody(const Pose::Result& b, int fw, int fh, Pose::Roi& roi) {
    if (!b.found) return false;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, seen = 0;
    for (int i = Pose::NOSE; i <= Pose::MOUTH_R; ++i) {
        if (b.visibility[i] < 0.5f) continue;
        x0 = std::min(x0, b.image[i].x * fw); x1 = std::max(x1, b.image[i].x * fw);
        y0 = std::min(y0, b.image[i].y * fh); y1 = std::max(y1, b.image[i].y * fh);
        ++seen;
    }
    if (seen < 5 || b.visibility[Pose::L_EYE] < 0.5f || b.visibility[Pose::R_EYE] < 0.5f) return false;
    const float ax = b.image[Pose::R_EYE].x * fw, ay = b.image[Pose::R_EYE].y * fh, bx = b.image[Pose::L_EYE].x * fw, by = b.image[Pose::L_EYE].y * fh;
    // the points span the ears and eyes to the mouth: the face reaches past them, above to the forehead, below to the chin
    roi.cx = (x0 + x1) / 2; roi.cy = (y0 + y1) / 2;
    roi.size = std::max(x1 - x0, (y1 - y0) * 2.2f) * 1.6f;
    roi.rot = wrapAngle(-std::atan2(-(by - ay), bx - ax));
    roi.valid = roi.size > 16;
    return roi.valid;
}

void headTurn(Result& r, int fw, int fh) {
    auto P = [&](int i) { return V3(r.image[i].x * fw, r.image[i].y * fh, r.image[i].z * fw); };
    const V3 across = P(L_EYE_OUT) - P(R_EYE_OUT);   // the performer's right eye to left: +x facing the camera
    const V3 down = P(CHIN) - P(FOREHEAD);           // +y
    r.head = M::frameXY(across, down);
    const V3 e = M::toEulerXYZ(r.head);
    r.pitch = e.x * 180 / M::PI; r.yaw = e.y * 180 / M::PI; r.roll = e.z * 180 / M::PI;
}

void eyesFromIrises(Result& r) {
    // each iris' middle between its eye's corners (in: towards the nose) and lids (up), as -1..1
    struct Eye { int out, in, up, down, inWeight, outWeight, upWeight, downWeight; };
    // the performer's right eye (33/133) and left eye (263/362); MediaPipe's names count from the performer
    const Eye eyes[2] = {{R_EYE_OUT, R_EYE_IN, 159, 145, 14, 16, 18, 12}, {L_EYE_OUT, L_EYE_IN, 386, 374, 13, 15, 17, 11}};
    for (const Eye& e : eyes) {
        const Pose::Point a = r.image[e.out], b = r.image[e.in];
        const float mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
        // whichever iris is nearer this eye
        int iris = R_IRIS;
        const float d0 = std::hypot(r.image[R_IRIS].x - mx, r.image[R_IRIS].y - my), d1 = std::hypot(r.image[L_IRIS].x - mx, r.image[L_IRIS].y - my);
        if (d1 < d0) iris = L_IRIS;
        const Pose::Point c = r.image[iris];
        const float ex = b.x - a.x, ey = b.y - a.y, len2 = ex * ex + ey * ey;
        if (len2 < 1e-10f) continue;
        const float t = ((c.x - a.x) * ex + (c.y - a.y) * ey) / len2;          // 0 at the outer corner, 1 at the inner
        const float h = std::clamp((t - 0.5f) / 0.18f, -1.0f, 1.0f);           // + : in
        const float lid = r.image[e.down].y - r.image[e.up].y;
        const float v = lid > 1e-6f ? std::clamp(((c.y - r.image[e.up].y) / lid - 0.5f) / 0.3f, -1.0f, 1.0f) : 0.0f;   // + : down
        r.weights[e.inWeight] = std::max(0.0f, h);
        r.weights[e.outWeight] = std::max(0.0f, -h);
        r.weights[e.downWeight] = std::max(0.0f, v);
        r.weights[e.upWeight] = std::max(0.0f, -v);
    }
}

bool Tracker::process(const Image& frame, const Pose::Result* body, const Settings& s, Result& out, std::string& err,
                      const std::function<void(Result&)>& adjust) {
    const auto t0 = std::chrono::steady_clock::now();
    out = Result();
    if (!loaded() || frame.empty()) return true;
    Pose::Roi roi = roi_;
    bool fresh = false;
    if (!have_) {
        if (!(s.fromBody && body && roiFromBody(*body, frame.w, frame.h, roi))) {
            float sc = 0;
            if (!detect(frame, s, roi, sc, err)) { if (!err.empty()) return false; prev_ = Result(); return true; }
            out.detected = true;
        }
        fresh = true;
    }
    Result r;
    if (!landmarks(frame, roi, r, err)) return false;
    if (r.score < s.presence && !fresh) {
        // lost while tracking: look for it again in this frame
        float sc = 0;
        have_ = false;
        if (!detect(frame, s, roi, sc, err)) { if (!err.empty()) return false; prev_ = Result(); return true; }
        out.detected = true;
        if (!landmarks(frame, roi, r, err)) return false;
    }
    if (r.score < s.presence) { have_ = false; prev_ = Result(); return true; }
    r.found = true;
    r.detected = out.detected;
    // smoothing against the last frame, more when it barely moved (a still face stays still)
    if (prev_.found && s.smooth > 0) {
        const float k = std::clamp(s.smooth, 0.0f, 0.95f);
        for (int i = 0; i < N; ++i) {
            Pose::Point& a = r.image[i]; const Pose::Point& b = prev_.image[i];
            a.x = a.x * (1 - k) + b.x * k; a.y = a.y * (1 - k) + b.y * k; a.z = a.z * (1 - k) + b.z * k;
        }
    }
    // the next region from the mesh as the model saw it (before any marker correction)
    roi_ = roiFromLandmarks(r, frame.w, frame.h, s.roiScale);
    have_ = roi_.valid;
    if (adjust) adjust(r);
    float w[BLENDSHAPES];
    if (!blendshapes(r, frame.w, frame.h, w, err)) return false;
    for (int i = 0; i < BLENDSHAPES; ++i) {
        float v = std::clamp(w[i] * s.gain, 0.0f, 1.0f);
        if (prev_.found && s.weightSmooth > 0) { const float k = std::clamp(s.weightSmooth, 0.0f, 0.95f); v = v * (1 - k) + prev_.weights[i] * k; }
        r.weights[i] = v;
    }
    if (s.eyesFromIris) eyesFromIrises(r);
    headTurn(r, frame.w, frame.h);
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    prev_ = r;
    out = r;
    return true;
}

const std::vector<std::pair<int, int>>& contour() { return CONTOUR_V; }

}  // namespace Face
