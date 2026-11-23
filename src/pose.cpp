// Pantomimus - BlazePose (see pose.h), and the ONNX Runtime plumbing (net.h).
#include "pose.h"

#include "onnxruntime_c_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------- ONNX Runtime
namespace Net {

static const OrtApi* ort = nullptr;
static OrtEnv* env = nullptr;
static std::string ortVersion;

static std::string exeDirectory() {
#ifdef _WIN32
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path().u8string();
#else
    char p[4096]; const ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
    if (n <= 0) return ".";
    p[n] = 0; return fs::path(p).parent_path().u8string();
#endif
}

bool runtime(std::string& err) {
    static std::mutex mu;
    std::lock_guard<std::mutex> l(mu);
    if (ort) return true;
    typedef const OrtApiBase*(ORT_API_CALL * GetBase)();
    GetBase getBase = nullptr;
#ifdef _WIN32
    HMODULE h = LoadLibraryW((fs::u8path(exeDirectory()) / "onnxruntime.dll").wstring().c_str());
    if (!h) h = LoadLibraryW(L"onnxruntime.dll");
    if (h) getBase = (GetBase)GetProcAddress(h, "OrtGetApiBase");
#else
    void* h = dlopen((exeDirectory() + "/libonnxruntime.so").c_str(), RTLD_NOW);
    if (!h) h = dlopen("libonnxruntime.so", RTLD_NOW);
    if (!h) h = dlopen("libonnxruntime.so.1", RTLD_NOW);
    if (h) getBase = (GetBase)dlsym(h, "OrtGetApiBase");
#endif
    if (!getBase) { err = "ONNX Runtime (onnxruntime.dll) isn't beside Pantomimus"; return false; }
    const OrtApiBase* base = getBase();
    ort = base->GetApi(ORT_API_VERSION);
    if (!ort) { err = std::string("This ONNX Runtime is too old (") + base->GetVersionString() + ")"; return false; }
    ortVersion = base->GetVersionString();
    if (OrtStatus* st = ort->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "pantomimus", &env)) {
        err = ort->GetErrorMessage(st); ort->ReleaseStatus(st); ort = nullptr; return false;
    }
    return true;
}
std::string runtimeVersion() { return ortVersion; }

static bool check(OrtStatus* st, std::string& err) {
    if (!st) return true;
    err = ort->GetErrorMessage(st);
    ort->ReleaseStatus(st);
    return false;
}

Model::~Model() { unload(); }
void Model::unload() {
    if (session_ && ort) ort->ReleaseSession((OrtSession*)session_);
    session_ = nullptr; outNames_.clear(); inName_.clear(); path_.clear();
}

bool Model::load(const std::string& path, int threads, std::string& err) {
    unload();
    if (!runtime(err)) return false;
    if (!fs::exists(fs::u8path(path))) { err = "The model isn't there: " + path; return false; }
    OrtSessionOptions* so = nullptr;
    if (!check(ort->CreateSessionOptions(&so), err)) return false;
    if (threads > 0) ort->SetIntraOpNumThreads(so, threads);
    ort->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
    OrtSession* s = nullptr;
#ifdef _WIN32
    const std::wstring wp = fs::u8path(path).wstring();
    const bool ok = check(ort->CreateSession(env, wp.c_str(), so, &s), err);
#else
    const bool ok = check(ort->CreateSession(env, path.c_str(), so, &s), err);
#endif
    ort->ReleaseSessionOptions(so);
    if (!ok) { err = "Can't load " + fs::u8path(path).filename().u8string() + ": " + err; return false; }
    session_ = s;
    path_ = path;
    OrtAllocator* al = nullptr;
    ort->GetAllocatorWithDefaultOptions(&al);
    char* name = nullptr;
    if (check(ort->SessionGetInputName(s, 0, al, &name), err)) { inName_ = name; al->Free(al, name); }
    size_t n = 0;
    ort->SessionGetOutputCount(s, &n);
    for (size_t i = 0; i < n; ++i)
        if (check(ort->SessionGetOutputName(s, i, al, &name), err)) { outNames_.push_back(name); al->Free(al, name); }
    return true;
}

bool Model::run(const float* input, const std::vector<long long>& shape, std::vector<std::vector<float>>& outputs, std::string& err) {
    if (!session_) { err = "No model"; return false; }
    OrtMemoryInfo* mi = nullptr;
    if (!check(ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi), err)) return false;
    size_t count = 1;
    for (long long d : shape) count *= (size_t)d;
    std::vector<int64_t> sh(shape.begin(), shape.end());
    OrtValue* in = nullptr;
    const bool made = check(ort->CreateTensorWithDataAsOrtValue(mi, (void*)input, count * sizeof(float), sh.data(), sh.size(),
                                                                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), err);
    ort->ReleaseMemoryInfo(mi);
    if (!made) return false;
    const char* inNames[] = {inName_.c_str()};
    std::vector<const char*> outN;
    for (const auto& s : outNames_) outN.push_back(s.c_str());
    std::vector<OrtValue*> outs(outN.size(), nullptr);
    const bool ran = check(ort->Run((OrtSession*)session_, nullptr, inNames, &in, 1, outN.data(), outN.size(), outs.data()), err);
    ort->ReleaseValue(in);
    if (!ran) return false;
    outputs.resize(outs.size());
    for (size_t i = 0; i < outs.size(); ++i) {
        float* p = nullptr;
        OrtTensorTypeAndShapeInfo* info = nullptr;
        size_t n = 0;
        if (check(ort->GetTensorMutableData(outs[i], (void**)&p), err) && check(ort->GetTensorTypeAndShape(outs[i], &info), err)) {
            ort->GetTensorShapeElementCount(info, &n);
            ort->ReleaseTensorTypeAndShapeInfo(info);
            outputs[i].assign(p, p + n);
        }
        ort->ReleaseValue(outs[i]);
    }
    return true;
}

}  // namespace Net

// ---------------------------------------------------------------- BlazePose
namespace Pose {

static const char* NAMES[N] = {"nose", "left eye (inner)", "left eye", "left eye (outer)", "right eye (inner)", "right eye",
                               "right eye (outer)", "left ear", "right ear", "mouth (left)", "mouth (right)", "left shoulder",
                               "right shoulder", "left elbow", "right elbow", "left wrist", "right wrist", "left pinky",
                               "right pinky", "left index", "right index", "left thumb", "right thumb", "left hip", "right hip",
                               "left knee", "right knee", "left ankle", "right ankle", "left heel", "right heel",
                               "left foot index", "right foot index"};
const char* landmarkName(int i) { return i >= 0 && i < N ? NAMES[i] : "?"; }

const int BONES[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 7}, {0, 4}, {4, 5}, {5, 6}, {6, 8}, {9, 10},
                        {11, 12}, {11, 13}, {13, 15}, {15, 17}, {15, 19}, {15, 21}, {17, 19},
                        {12, 14}, {14, 16}, {16, 18}, {16, 20}, {16, 22}, {18, 20},
                        {11, 23}, {12, 24}, {23, 24}, {23, 25}, {24, 26}, {25, 27}, {26, 28},
                        {27, 29}, {28, 30}, {29, 31}, {30, 32}, {27, 31}, {28, 32}};
const int BONE_COUNT = sizeof(BONES) / sizeof(BONES[0]);

static const char* MODEL_FILES[3] = {"pose_landmarks_detector_lite.onnx", "pose_landmarks_detector_full.onnx", "pose_landmarks_detector_heavy.onnx"};
const char* modelFile(int m) { return MODEL_FILES[std::clamp(m, 0, 2)]; }

std::vector<float> anchors() {
    // MediaPipe's SsdAnchorsCalculator, pose_detection: 5 layers, strides 8 16 32 32 32, aspect ratio 1 plus the
    // interpolated one, fixed anchor size; layers with the same stride share their cells
    const int strides[5] = {8, 16, 32, 32, 32};
    std::vector<float> a;
    int layer = 0;
    while (layer < 5) {
        int per = 0, last = layer;
        while (last < 5 && strides[last] == strides[layer]) { per += 2; ++last; }
        const int fm = (int)std::ceil(224.0f / strides[layer]);
        for (int y = 0; y < fm; ++y)
            for (int x = 0; x < fm; ++x)
                for (int k = 0; k < per; ++k) { a.push_back((x + 0.5f) / fm); a.push_back((y + 0.5f) / fm); a.push_back(1); a.push_back(1); }
        layer = last;
    }
    return a;
}

static float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-std::clamp(x, -80.0f, 80.0f))); }
static float wrapAngle(float a) { return a - 2 * 3.14159265f * std::floor((a + 3.14159265f) / (2 * 3.14159265f)); }

// MediaPipe's AlignmentPointsRectsCalculator then RectTransformationCalculator: centre on a, turned so a->b points up,
// side twice |ab|, scaled
static Roi alignedRoi(float ax, float ay, float bx, float by, float scale) {
    Roi r;
    r.cx = ax; r.cy = ay;
    r.size = 2 * std::hypot(bx - ax, by - ay) * scale;
    r.rot = wrapAngle(3.14159265f / 2 - std::atan2(-(by - ay), bx - ax));
    r.valid = r.size > 4;
    return r;
}

bool Tracker::load(const std::string& dir, const Settings& s, std::string& err) {
    const std::string d = fs::u8path(dir).u8string();
    if (!det_.loaded() || dir_ != d || threads_ != s.threads)
        if (!det_.load((fs::u8path(dir) / DETECTOR_FILE).u8string(), s.threads, err)) return false;
    if (!lm_.loaded() || dir_ != d || model_ != s.model || threads_ != s.threads) {
        if (!lm_.load((fs::u8path(dir) / modelFile(s.model)).u8string(), s.threads, err)) return false;
        model_ = s.model;
    }
    dir_ = d; threads_ = s.threads;
    if (anchors_.empty()) anchors_ = anchors();
    roi_ = Roi();
    return true;
}

bool Tracker::detect(const Image& frame, const Settings& s, Roi& roi, float& best, std::string& err) {
    const int n = 224;
    buf_.resize((size_t)n * n * 3);
    const Img::Letterbox L = Img::letterbox(frame, n, -1, 1, buf_.data());
    std::vector<std::vector<float>> out;
    if (!det_.run(buf_.data(), {1, n, n, 3}, out, err)) return false;
    if (out.size() < 2) { err = "The detector gave too few outputs"; return false; }
    const std::vector<float>& box = out[0].size() >= out[1].size() ? out[0] : out[1];
    const std::vector<float>& sc = out[0].size() >= out[1].size() ? out[1] : out[0];
    const size_t na = std::min(sc.size(), anchors_.size() / 4);
    if (box.size() < na * 12) { err = "The detector's boxes aren't 12 numbers each"; return false; }
    // the best box, then MediaPipe's weighted NMS: the boxes overlapping it averaged by score
    int bi = -1; best = 0;
    for (size_t i = 0; i < na; ++i) { const float p = sigmoid(sc[i]); if (p > best) { best = p; bi = (int)i; } }
    if (bi < 0 || best < s.detectScore) return false;
    auto decode = [&](size_t i, float* o) {   // cx cy w h then 4 keypoints, 0..1 of the 224 square
        const float* b = &box[i * 12];
        const float* a = &anchors_[i * 4];
        o[0] = b[0] / n * a[2] + a[0]; o[1] = b[1] / n * a[3] + a[1]; o[2] = b[2] / n * a[2]; o[3] = b[3] / n * a[3];
        for (int k = 0; k < 4; ++k) { o[4 + k * 2] = b[4 + k * 2] / n * a[2] + a[0]; o[5 + k * 2] = b[5 + k * 2] / n * a[3] + a[1]; }
    };
    float ref[12]; decode(bi, ref);
    auto iou = [](const float* p, const float* q) {
        const float ax0 = p[0] - p[2] / 2, ax1 = p[0] + p[2] / 2, ay0 = p[1] - p[3] / 2, ay1 = p[1] + p[3] / 2;
        const float bx0 = q[0] - q[2] / 2, bx1 = q[0] + q[2] / 2, by0 = q[1] - q[3] / 2, by1 = q[1] + q[3] / 2;
        const float iw = std::max(0.0f, std::min(ax1, bx1) - std::max(ax0, bx0)), ih = std::max(0.0f, std::min(ay1, by1) - std::max(ay0, by0));
        const float in = iw * ih, un = p[2] * p[3] + q[2] * q[3] - in;
        return un > 0 ? in / un : 0.0f;
    };
    float acc[12] = {}, wsum = 0;
    for (size_t i = 0; i < na; ++i) {
        const float p = sigmoid(sc[i]);
        if (p < s.detectScore) continue;
        float d[12]; decode(i, d);
        if (iou(ref, d) < 0.3f) continue;
        for (int k = 0; k < 12; ++k) acc[k] += d[k] * p;
        wsum += p;
    }
    if (wsum <= 0) return false;
    for (float& v : acc) v /= wsum;
    // to frame pixels
    auto px = [&](float u, float v, float& x, float& y) { x = (u * n - L.padX) / L.scale; y = (v * n - L.padY) / L.scale; };
    float hx, hy, tx, ty;
    px(acc[4], acc[5], hx, hy);   // keypoint 0: the hips' middle
    px(acc[6], acc[7], tx, ty);   // keypoint 1: above the head, the body's size and turn
    roi = alignedRoi(hx, hy, tx, ty, s.roiScale);
    return roi.valid;
}

bool Tracker::landmarks(const Image& frame, const Roi& roi, const Settings& s, Result& out, std::string& err) {
    const int n = 256;
    buf_.resize((size_t)n * n * 3);
    Img::cropRotated(frame, roi.cx, roi.cy, roi.size, roi.size, roi.rot, n, 0, 1, buf_.data());
    std::vector<std::vector<float>> o;
    if (!lm_.run(buf_.data(), {1, n, n, 3}, o, err)) return false;
    // the outputs by size: 195 landmarks (39 x 5), 1 the flag, 64x64x39 heatmap, 117 world (39 x 3), 256x256 mask
    const std::vector<float>*lm = nullptr, *flag = nullptr, *heat = nullptr, *world = nullptr;
    for (const auto& v : o) {
        if (v.size() == 195) lm = &v;
        else if (v.size() == 1) flag = &v;
        else if (v.size() == 64 * 64 * 39) heat = &v;
        else if (v.size() == 117) world = &v;
    }
    if (!lm || !flag || !world) { err = "The landmark model's outputs aren't BlazePose's"; return false; }
    out.score = (*flag)[0];
    if (out.score > 1 || out.score < 0) out.score = sigmoid(out.score);
    const float c = std::cos(roi.rot), sn = std::sin(roi.rot);
    float lx[39], ly[39];
    for (int i = 0; i < 39; ++i) { lx[i] = (*lm)[i * 5] / n; ly[i] = (*lm)[i * 5 + 1] / n; }
    if (heat && s.refine) {
        // MediaPipe's RefineLandmarksFromHeatmap: kernel 7, the heatmap's sigmoid as weights, when sure enough
        const int H = 64, K = 3;
        for (int i = 0; i < N; ++i) {
            const int cx = (int)(lx[i] * H), cy = (int)(ly[i] * H);
            float sum = 0, sx = 0, sy = 0, mx = 0;
            for (int y = std::max(0, cy - K); y <= std::min(H - 1, cy + K); ++y)
                for (int x = std::max(0, cx - K); x <= std::min(H - 1, cx + K); ++x) {
                    const float v = sigmoid((*heat)[((size_t)y * H + x) * 39 + i]);
                    sum += v; sx += v * x; sy += v * y; mx = std::max(mx, v);
                }
            if (mx >= 0.5f && sum > 0) { lx[i] = sx / sum / H; ly[i] = sy / sum / H; }
        }
    }
    for (int i = 0; i < 39; ++i) {
        const float u = (lx[i] - 0.5f) * roi.size, v = (ly[i] - 0.5f) * roi.size;
        const float x = roi.cx + u * c - v * sn, y = roi.cy + u * sn + v * c;
        if (i < N) {
            out.image[i].x = x / frame.w; out.image[i].y = y / frame.h;
            out.image[i].z = (*lm)[i * 5 + 2] / n * roi.size / frame.w;
            out.visibility[i] = sigmoid((*lm)[i * 5 + 3]);
            out.presence[i] = sigmoid((*lm)[i * 5 + 4]);
            // world: turned back by the region's turn (MediaPipe's WorldLandmarkProjection)
            const float wx = (*world)[i * 3], wy = (*world)[i * 3 + 1];
            out.world[i] = {c * wx - sn * wy, sn * wx + c * wy, (*world)[i * 3 + 2]};
        } else if (i == 34) {
            // the auxiliary points 33 (the hips' middle) and 34 (above the head): the next frame's region
            const float u0 = (lx[33] - 0.5f) * roi.size, v0 = (ly[33] - 0.5f) * roi.size;
            const float ax = roi.cx + u0 * c - v0 * sn, ay = roi.cy + u0 * sn + v0 * c;
            out.roi = alignedRoi(ax, ay, x, y, s.roiScale);
        }
    }
    return true;
}

bool Tracker::process(const Image& frame, const Settings& s, Result& out, std::string& err) {
    const auto t0 = std::chrono::steady_clock::now();
    out = Result();
    out.frameW = frame.w; out.frameH = frame.h;
    if (frame.empty()) return true;
    if (!loaded()) { err = "The pose models aren't loaded"; return false; }
    Roi roi = roi_;
    if (!roi.valid) {
        float score = 0;
        if (!detect(frame, s, roi, score, err)) {
            out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            return err.empty();
        }
        out.detected = true;
    }
    Result r; r.frameW = frame.w; r.frameH = frame.h; r.detected = out.detected;
    if (!landmarks(frame, roi, s, r, err)) return false;
    r.found = r.score >= s.presence;
    roi_ = r.found ? r.roi : Roi();
    r.roi.valid = r.found;
    Roi used = roi; used.valid = true;
    r.roi = used;
    out = r;
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

}  // namespace Pose
