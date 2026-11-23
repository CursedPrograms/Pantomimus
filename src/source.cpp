// Pantomimus - where the pictures come from (see source.h)
#include "source.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#else
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Src {

bool isPicture(const std::string& path) {
    const std::string e = Util::extension(path);
    return e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".bmp" || e == ".tga" || e == ".gif" || e == ".psd";
}

// ---------------------------------------------------------------- a still picture
class Picture : public Source {
public:
    Image im; std::string path; double t0 = Util::now(); long long n = 0;
    bool read(Image& out, double& t, std::string&) override {
        // 30 frames a second of the same picture
        const double due = t0 + n / 30.0, wait = due - Util::now();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        out = im; t = n / 30.0; ++n;
        return true;
    }
    bool isStill() const override { return true; }
    std::string describe() const override { return Util::fileName(path) + ", " + std::to_string(im.w) + " x " + std::to_string(im.h); }
};

std::unique_ptr<Source> openPicture(const std::string& path, std::string& err) {
    auto p = std::make_unique<Picture>();
    if (!Img::load(path, p->im, err)) return nullptr;
    p->path = path;
    return p;
}

std::unique_ptr<Source> openFile(const std::string& path, std::string& err) {
    return isPicture(path) ? openPicture(path, err) : openVideo(path, err);
}

#ifdef _WIN32
// ---------------------------------------------------------------- Media Foundation
template <class T> static void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

static bool mfUp() {
    static bool ok = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // this thread too (harmless if it is already)
    return ok;
}

static std::string subtypeName(const GUID& g) {
    if (g == MFVideoFormat_MJPG) return "MJPG";
    if (g == MFVideoFormat_YUY2) return "YUY2";
    if (g == MFVideoFormat_NV12) return "NV12";
    if (g == MFVideoFormat_RGB24) return "RGB24";
    if (g == MFVideoFormat_RGB32) return "RGB32";
    if (g == MFVideoFormat_H264) return "H264";
    if (g == MFVideoFormat_I420) return "I420";
    char b[8] = {0}; memcpy(b, &g.Data1, 4);
    for (int i = 0; i < 4; ++i) if (b[i] < 32 || b[i] > 126) b[i] = '?';
    return b;
}

static std::string narrow(const wchar_t* w) { return w ? fs::path(w).u8string() : std::string(); }

static std::vector<IMFActivate*> deviceList() {
    std::vector<IMFActivate*> r;
    if (!mfUp()) return r;
    IMFAttributes* a = nullptr;
    if (FAILED(MFCreateAttributes(&a, 1))) return r;
    a->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** acts = nullptr; UINT32 n = 0;
    if (SUCCEEDED(MFEnumDeviceSources(a, &acts, &n))) {
        for (UINT32 i = 0; i < n; ++i) r.push_back(acts[i]);
        CoTaskMemFree(acts);
    }
    release(a);
    return r;
}

static void formatsOf(IMFMediaSource* src, std::vector<Format>& out, std::vector<IMFMediaType*>* types) {
    IMFPresentationDescriptor* pd = nullptr; IMFStreamDescriptor* sd = nullptr; IMFMediaTypeHandler* h = nullptr;
    BOOL sel = FALSE;
    if (SUCCEEDED(src->CreatePresentationDescriptor(&pd)) && SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &sel, &sd)) && SUCCEEDED(sd->GetMediaTypeHandler(&h))) {
        DWORD n = 0; h->GetMediaTypeCount(&n);
        for (DWORD i = 0; i < n; ++i) {
            IMFMediaType* t = nullptr;
            if (FAILED(h->GetMediaTypeByIndex(i, &t))) continue;
            UINT32 w = 0, hh = 0, num = 0, den = 1; GUID st{};
            MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &hh);
            MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
            t->GetGUID(MF_MT_SUBTYPE, &st);
            Format f; f.w = (int)w; f.h = (int)hh; f.fps = den ? (float)num / den : 0; f.kind = subtypeName(st);
            out.push_back(f);
            if (types) types->push_back(t); else release(t);
        }
    }
    release(h); release(sd); release(pd);
}

std::vector<Device> cameras() {
    std::vector<Device> r;
    for (IMFActivate* a : deviceList()) {
        Device d;
        WCHAR* s = nullptr; UINT32 len = 0;
        if (SUCCEEDED(a->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &s, &len))) { d.name = narrow(s); CoTaskMemFree(s); }
        if (SUCCEEDED(a->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &s, &len))) { d.id = narrow(s); CoTaskMemFree(s); }
        IMFMediaSource* src = nullptr;
        if (SUCCEEDED(a->ActivateObject(IID_PPV_ARGS(&src)))) {
            std::vector<Format> fs_;
            formatsOf(src, fs_, nullptr);
            // the distinct modes, biggest first
            for (const Format& f : fs_) {
                bool dup = false;
                for (Format& g : d.formats) if (g.w == f.w && g.h == f.h && std::fabs(g.fps - f.fps) < 0.5f) { dup = true; break; }
                if (!dup && f.w > 0) d.formats.push_back(f);
            }
            std::sort(d.formats.begin(), d.formats.end(), [](const Format& a, const Format& b) { return a.w * a.h != b.w * b.h ? a.w * a.h > b.w * b.h : a.fps > b.fps; });
            src->Shutdown(); release(src);
            a->ShutdownObject();
        }
        release(a);
        r.push_back(d);
    }
    return r;
}

// reads RGB32 frames from a source reader into RGB
class MfReader : public Source {
public:
    IMFSourceReader* reader = nullptr;
    IMFMediaSource* source = nullptr;
    int w = 0, h = 0; LONG stride = 0;
    float rate = 30; double dur = 0;
    bool file = false;
    std::string what;
    LONGLONG first = -1;
    ~MfReader() override {
        release(reader);
        if (source) { source->Shutdown(); release(source); }
    }
    bool setup(std::string& err) {
        IMFMediaType* t = nullptr;
        if (FAILED(MFCreateMediaType(&t))) { err = "Media Foundation: no media type"; return false; }
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        const HRESULT hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, t);
        release(t);
        if (FAILED(hr)) { err = "This video can't be turned into RGB pictures"; return false; }
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        IMFMediaType* cur = nullptr;
        if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
            UINT32 ww = 0, hh = 0, num = 0, den = 1;
            MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &ww, &hh);
            if (SUCCEEDED(MFGetAttributeRatio(cur, MF_MT_FRAME_RATE, &num, &den)) && den && num) rate = (float)num / den;
            w = (int)ww; h = (int)hh;
            stride = (LONG)MFGetAttributeUINT32(cur, MF_MT_DEFAULT_STRIDE, (UINT32)(w * 4));
            release(cur);
        }
        if (w <= 0 || h <= 0) { err = "The video has no picture size"; return false; }
        return true;
    }
    bool read(Image& out, double& t, std::string& err) override {
        for (int tries = 0; tries < 200; ++tries) {
            DWORD idx = 0, flags = 0; LONGLONG ts = 0; IMFSample* s = nullptr;
            const HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s);
            if (FAILED(hr)) { err = "The camera stopped (is another program using it?)"; return false; }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { release(s); err = "end"; return false; }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) { std::string e; setup(e); }
            if (!s) continue;
            IMFMediaBuffer* b = nullptr;
            bool ok = false;
            if (SUCCEEDED(s->GetBufferByIndex(0, &b))) {
                IMF2DBuffer* b2 = nullptr;
                BYTE* p = nullptr; LONG pitch = 0;
                if (SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&p, &pitch))) {
                    copy(p, pitch, out); b2->Unlock2D(); ok = true;
                } else {
                    DWORD max = 0, len = 0;
                    if (SUCCEEDED(b->Lock(&p, &max, &len))) {
                        // a plain buffer: the default stride says if it's upside down
                        const LONG st = stride ? stride : w * 4;
                        BYTE* top = st < 0 ? p + (size_t)(-st) * (h - 1) : p;
                        if (len >= (DWORD)(std::abs(st) * h)) { copy(top, st, out); ok = true; }
                        b->Unlock();
                    }
                }
                release(b2);
            }
            release(b);
            release(s);
            if (!ok) continue;
            if (first < 0) first = ts;
            t = (ts - (file ? 0 : first)) / 1e7;
            return true;
        }
        err = "No pictures from the source";
        return false;
    }
    void copy(const BYTE* p, LONG pitch, Image& out) {
        out.w = w; out.h = h; out.rgb.resize((size_t)w * h * 3);
        for (int y = 0; y < h; ++y) {
            const BYTE* row = p + (ptrdiff_t)pitch * y;
            uint8_t* o = &out.rgb[(size_t)y * w * 3];
            for (int x = 0; x < w; ++x) { o[x * 3] = row[x * 4 + 2]; o[x * 3 + 1] = row[x * 4 + 1]; o[x * 3 + 2] = row[x * 4]; }
        }
    }
    bool isFile() const override { return file; }
    double duration() const override { return dur; }
    float fps() const override { return rate; }
    bool seek(double t) override {
        if (!file) return false;
        PROPVARIANT v; PropVariantInit(&v);
        v.vt = VT_I8; v.hVal.QuadPart = (LONGLONG)(t * 1e7);
        const bool ok = SUCCEEDED(reader->SetCurrentPosition(GUID_NULL, v));
        PropVariantClear(&v);
        return ok;
    }
    std::string describe() const override {
        char b[64]; snprintf(b, sizeof b, ", %d x %d, %.0f fps", w, h, rate);
        return what + b;
    }
};

static IMFAttributes* readerAttributes() {
    IMFAttributes* a = nullptr;
    if (FAILED(MFCreateAttributes(&a, 2))) return nullptr;
    a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // YUV and MJPG decoded and turned into RGB
    a->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
    return a;
}

std::unique_ptr<Source> openCamera(int index, int w, int h, float fps, std::string& err) {
    std::vector<IMFActivate*> list = deviceList();
    if (index < 0 || index >= (int)list.size()) { for (auto* a : list) release(a); err = "No camera"; return nullptr; }
    auto r = std::make_unique<MfReader>();
    WCHAR* s = nullptr; UINT32 len = 0;
    if (SUCCEEDED(list[index]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &s, &len))) { r->what = narrow(s); CoTaskMemFree(s); }
    const HRESULT hr = list[index]->ActivateObject(IID_PPV_ARGS(&r->source));
    for (auto* a : list) release(a);
    if (FAILED(hr)) { err = "The camera can't be opened (is another program using it?)"; return nullptr; }
    // the mode: the one nearest the wanted size, then frame rate; MJPG/YUY2 over others at the same size
    std::vector<Format> fm; std::vector<IMFMediaType*> types;
    formatsOf(r->source, fm, &types);
    int best = -1; double bestScore = 1e18;
    for (size_t i = 0; i < fm.size(); ++i) {
        const Format& f = fm[i];
        if (f.w <= 0) continue;
        double sc;
        if (w > 0 && h > 0) sc = std::fabs((double)f.w * f.h - (double)w * h) * 10 + std::fabs(f.w - w) + std::fabs(f.h - h);
        else sc = -(double)std::min(f.w * f.h, 1920 * 1080);   // the best up to full HD
        sc += std::fabs(f.fps - (fps > 0 ? fps : 30)) * 1000;
        if (f.kind != "MJPG" && f.kind != "YUY2" && f.kind != "NV12") sc += 500;
        if (sc < bestScore) { bestScore = sc; best = (int)i; }
    }
    if (best >= 0) {
        IMFPresentationDescriptor* pd = nullptr; IMFStreamDescriptor* sd = nullptr; IMFMediaTypeHandler* th = nullptr; BOOL sel = FALSE;
        if (SUCCEEDED(r->source->CreatePresentationDescriptor(&pd)) && SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &sel, &sd)) && SUCCEEDED(sd->GetMediaTypeHandler(&th)))
            th->SetCurrentMediaType(types[best]);
        release(th); release(sd); release(pd);
    }
    for (auto* t : types) release(t);
    IMFAttributes* a = readerAttributes();
    const HRESULT hr2 = MFCreateSourceReaderFromMediaSource(r->source, a, &r->reader);
    release(a);
    if (FAILED(hr2)) { err = "The camera's reader can't be made"; return nullptr; }
    if (!r->setup(err)) return nullptr;
    return r;
}

std::unique_ptr<Source> openVideo(const std::string& path, std::string& err) {
    if (!mfUp()) { err = "Media Foundation isn't available"; return nullptr; }
    auto r = std::make_unique<MfReader>();
    r->file = true; r->what = Util::fileName(path);
    IMFAttributes* a = readerAttributes();
    const HRESULT hr = MFCreateSourceReaderFromURL(fs::u8path(path).wstring().c_str(), a, &r->reader);
    release(a);
    if (FAILED(hr)) { err = "Can't open the video " + Util::fileName(path); return nullptr; }
    if (!r->setup(err)) return nullptr;
    PROPVARIANT v; PropVariantInit(&v);
    if (SUCCEEDED(r->reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v))) r->dur = v.uhVal.QuadPart / 1e7;
    PropVariantClear(&v);
    return r;
}

#else
// ---------------------------------------------------------------- Linux: V4L2 cameras, ffmpeg for videos
std::vector<Device> cameras() {
    std::vector<Device> r;
    for (int i = 0; i < 16; ++i) {
        const std::string path = "/dev/video" + std::to_string(i);
        const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 && (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
            Device d; d.name = (const char*)cap.card; d.id = path;
            v4l2_frmsizeenum fs_{}; fs_.pixel_format = V4L2_PIX_FMT_YUYV;
            for (fs_.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs_) == 0; ++fs_.index)
                if (fs_.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    v4l2_frmivalenum fi{}; fi.pixel_format = V4L2_PIX_FMT_YUYV; fi.width = fs_.discrete.width; fi.height = fs_.discrete.height;
                    for (fi.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0; ++fi.index)
                        if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE && fi.discrete.numerator)
                            d.formats.push_back({(int)fs_.discrete.width, (int)fs_.discrete.height, (float)fi.discrete.denominator / fi.discrete.numerator, "YUYV"});
                }
            r.push_back(d);
        }
        close(fd);
    }
    return r;
}

class V4l2 : public Source {
public:
    int fd = -1, w = 0, h = 0; float rate = 30; std::string what;
    std::vector<std::pair<void*, size_t>> bufs;
    double t0 = -1;
    ~V4l2() override {
        if (fd >= 0) { v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE; ioctl(fd, VIDIOC_STREAMOFF, &t); }
        for (auto& b : bufs) munmap(b.first, b.second);
        if (fd >= 0) close(fd);
    }
    bool read(Image& out, double& t, std::string& err) override {
        v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) { err = "The camera stopped"; return false; }
        const uint8_t* p = (const uint8_t*)bufs[b.index].first;
        out.w = w; out.h = h; out.rgb.resize((size_t)w * h * 3);
        for (int i = 0; i < w * h / 2; ++i) {   // YUYV to RGB
            const int y0 = p[i * 4], u = p[i * 4 + 1] - 128, y1 = p[i * 4 + 2], v = p[i * 4 + 3] - 128;
            for (int k = 0; k < 2; ++k) {
                const int y = k ? y1 : y0;
                uint8_t* o = &out.rgb[(size_t)(i * 2 + k) * 3];
                o[0] = (uint8_t)std::clamp(y + (int)(1.402f * v), 0, 255);
                o[1] = (uint8_t)std::clamp(y - (int)(0.344f * u + 0.714f * v), 0, 255);
                o[2] = (uint8_t)std::clamp(y + (int)(1.772f * u), 0, 255);
            }
        }
        ioctl(fd, VIDIOC_QBUF, &b);
        const double now = Util::now();
        if (t0 < 0) t0 = now;
        t = now - t0;
        return true;
    }
    float fps() const override { return rate; }
    std::string describe() const override { char b[64]; snprintf(b, sizeof b, ", %d x %d, %.0f fps", w, h, rate); return what + b; }
};

std::unique_ptr<Source> openCamera(int index, int w, int h, float fps, std::string& err) {
    const std::vector<Device> cams = cameras();
    if (index < 0 || index >= (int)cams.size()) { err = "No camera"; return nullptr; }
    auto c = std::make_unique<V4l2>();
    c->what = cams[index].name;
    c->fd = open(cams[index].id.c_str(), O_RDWR);
    if (c->fd < 0) { err = "The camera can't be opened"; return nullptr; }
    v4l2_format f{}; f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    f.fmt.pix.width = w > 0 ? w : 1280; f.fmt.pix.height = h > 0 ? h : 720; f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; f.fmt.pix.field = V4L2_FIELD_ANY;
    if (ioctl(c->fd, VIDIOC_S_FMT, &f) < 0) { err = "The camera won't give YUYV pictures"; return nullptr; }
    c->w = f.fmt.pix.width; c->h = f.fmt.pix.height;
    v4l2_streamparm sp{}; sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sp.parm.capture.timeperframe.numerator = 1; sp.parm.capture.timeperframe.denominator = (unsigned)(fps > 0 ? fps : 30);
    ioctl(c->fd, VIDIOC_S_PARM, &sp);
    if (sp.parm.capture.timeperframe.numerator) c->rate = (float)sp.parm.capture.timeperframe.denominator / sp.parm.capture.timeperframe.numerator;
    v4l2_requestbuffers rb{}; rb.count = 4; rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(c->fd, VIDIOC_REQBUFS, &rb) < 0) { err = "The camera has no buffers"; return nullptr; }
    for (unsigned i = 0; i < rb.count; ++i) {
        v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        ioctl(c->fd, VIDIOC_QUERYBUF, &b);
        void* m = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, b.m.offset);
        c->bufs.push_back({m, b.length});
        ioctl(c->fd, VIDIOC_QBUF, &b);
    }
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(c->fd, VIDIOC_STREAMON, &t) < 0) { err = "The camera won't start"; return nullptr; }
    return c;
}

// a video through ffmpeg (raw RGB on a pipe)
class Ffmpeg : public Source {
public:
    FILE* pipe = nullptr; int w = 0, h = 0; float rate = 30; double dur = 0; long long n = 0; std::string path;
    ~Ffmpeg() override { if (pipe) pclose(pipe); }
    bool start(double from) {
        if (pipe) pclose(pipe);
        char cmd[4096];
        snprintf(cmd, sizeof cmd, "ffmpeg -v quiet -ss %.3f -i \"%s\" -f rawvideo -pix_fmt rgb24 - 2>/dev/null", from, path.c_str());
        pipe = popen(cmd, "r");
        n = (long long)(from * rate);
        return pipe != nullptr;
    }
    bool read(Image& out, double& t, std::string& err) override {
        out.w = w; out.h = h; out.rgb.resize((size_t)w * h * 3);
        if (!pipe || fread(out.rgb.data(), 1, out.rgb.size(), pipe) != out.rgb.size()) { err = "end"; return false; }
        t = n / rate; ++n;
        return true;
    }
    bool isFile() const override { return true; }
    double duration() const override { return dur; }
    float fps() const override { return rate; }
    bool seek(double t) override { return start(t); }
    std::string describe() const override { char b[64]; snprintf(b, sizeof b, ", %d x %d, %.0f fps", w, h, rate); return Util::fileName(path) + b; }
};

std::unique_ptr<Source> openVideo(const std::string& path, std::string& err) {
    auto f = std::make_unique<Ffmpeg>();
    f->path = path;
    const std::string probe = "ffprobe -v quiet -select_streams v:0 -show_entries stream=width,height,r_frame_rate:format=duration -of csv=p=0 \"" + path + "\" 2>/dev/null";
    FILE* p = popen(probe.c_str(), "r");
    if (!p) { err = "ffmpeg isn't installed"; return nullptr; }
    char line[256]; int num = 30, den = 1;
    if (fgets(line, sizeof line, p)) sscanf(line, "%d,%d,%d/%d", &f->w, &f->h, &num, &den);
    if (fgets(line, sizeof line, p)) f->dur = atof(line);
    pclose(p);
    if (f->w <= 0) { err = "Can't read the video (is ffmpeg installed?)"; return nullptr; }
    f->rate = den ? (float)num / den : 30;
    if (!f->start(0)) { err = "ffmpeg won't start"; return nullptr; }
    return f;
}
#endif

}  // namespace Src
