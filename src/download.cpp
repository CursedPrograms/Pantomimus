// Pantomimus - fetching the models (see download.h)
#include "download.h"
#include "util.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace fs = std::filesystem;

namespace Download {

const std::vector<Model>& models() {
    static const std::vector<Model> m = {
        {"pose_detection.onnx", "https://huggingface.co/unity/inference-engine-blaze-pose/resolve/main/models/pose_detection.onnx", 15058101, "the person detector"},
        {"pose_landmarks_detector_lite.onnx", "https://huggingface.co/unity/inference-engine-blaze-pose/resolve/main/models/pose_landmarks_detector_lite.onnx", 5533849, "the pose model (lite)"},
        {"pose_landmarks_detector_full.onnx", "https://huggingface.co/unity/inference-engine-blaze-pose/resolve/main/models/pose_landmarks_detector_full.onnx", 12765405, "the pose model (full)"},
        {"pose_landmarks_detector_heavy.onnx", "https://huggingface.co/unity/inference-engine-blaze-pose/resolve/main/models/pose_landmarks_detector_heavy.onnx", 55181539, "the pose model (heavy)"},
        {"hand_detector.onnx", "https://huggingface.co/unity/inference-engine-blaze-hand/resolve/main/models/hand_detector.onnx", 4589374, "the palm detector"},
        {"hand_landmarks_detector.onnx", "https://huggingface.co/unity/inference-engine-blaze-hand/resolve/main/models/hand_landmarks_detector.onnx", 10903207, "the hand model"},
    };
    return m;
}

std::vector<Model> needed(int poseModel, bool hands) {
    const auto& m = models();
    std::vector<Model> r = {m[0], m[1 + std::max(0, std::min(2, poseModel))]};
    if (hands) r.push_back(m[5]);   // the hands are found from the body: the palm detector is not needed
    return r;
}

std::vector<Model> missing(const std::string& dir, int poseModel, bool hands) {
    std::vector<Model> r;
    for (const Model& m : needed(poseModel, hands))
        if (Util::fileSize(Util::join(dir, m.file)) < m.size / 2) r.push_back(m);
    return r;
}

#ifdef _WIN32
static bool getOnce(const std::wstring& url, const std::string& path, const Progress& progress, std::string& err, std::wstring& redirect, int& status) {
    URL_COMPONENTS u{}; u.dwStructSize = sizeof u;
    wchar_t host[512] = {0}, rest[4096] = {0};
    u.lpszHostName = host; u.dwHostNameLength = 512; u.lpszUrlPath = rest; u.dwUrlPathLength = 4096;
    wchar_t extra[4096] = {0}; u.lpszExtraInfo = extra; u.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) { err = "A bad address"; return false; }
    std::wstring object = std::wstring(rest) + extra;
    HINTERNET s = WinHttpOpen(L"Pantomimus", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"Pantomimus", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { err = "No internet (WinHTTP)"; return false; }
    HINTERNET c = WinHttpConnect(s, host, u.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = false;
    if (r) {
        DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;   // followed by hand, so the new host is reached too
        WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof noRedirect);
        if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof code;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            if (code >= 300 && code < 400) {
                wchar_t loc[4096] = {0}; DWORD ls = sizeof loc;
                if (WinHttpQueryHeaders(r, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &ls, WINHTTP_NO_HEADER_INDEX)) {
                    redirect = loc;
                    if (!redirect.empty() && redirect[0] == L'/') redirect = std::wstring(u.nScheme == INTERNET_SCHEME_HTTPS ? L"https://" : L"http://") + host + redirect;
                    ok = true;
                } else err = "A redirect without a place";
            } else if (code == 200) {
                long long total = 0;
                wchar_t len[64] = {0}; DWORD ls = sizeof len;
                if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &ls, WINHTTP_NO_HEADER_INDEX)) total = _wtoi64(len);
                const std::string part = path + ".part";
                std::ofstream out(fs::u8path(part), std::ios::binary);
                if (!out) err = "Can't write " + part;
                else {
                    std::vector<char> buf(1 << 16);
                    long long done = 0;
                    ok = true;
                    for (;;) {
                        DWORD got = 0;
                        if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &got)) { ok = false; err = "The download broke off"; break; }
                        if (got == 0) break;
                        out.write(buf.data(), got);
                        done += got;
                        if (progress && !progress(done, total)) { ok = false; err = "Stopped"; break; }
                    }
                    out.close();
                    if (ok && total > 0 && done != total) { ok = false; err = "The download came short"; }
                    std::error_code ec;
                    if (ok) { fs::remove(fs::u8path(path), ec); fs::rename(fs::u8path(part), fs::u8path(path), ec); if (ec) { ok = false; err = "Can't put " + path + " in place"; } }
                    else fs::remove(fs::u8path(part), ec);
                }
            } else err = "The server said " + std::to_string(code);
        } else err = "Can't reach the server (no internet?)";
        WinHttpCloseHandle(r);
    } else err = "Can't reach the server";
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#endif

bool get(const std::string& url, const std::string& path, const Progress& progress, std::string& err) {
#ifdef _WIN32
    std::wstring u = fs::u8path(url).wstring();
    for (int hop = 0; hop < 8; ++hop) {
        std::wstring next; int status = 0;
        if (!getOnce(u, path, progress, err, next, status)) return false;
        if (next.empty()) return true;
        u = next;
    }
    err = "Too many redirects";
    return false;
#else
    (void)progress;
    const std::string cmd = "curl -sfL -o \"" + path + ".part\" \"" + url + "\" && mv \"" + path + ".part\" \"" + path + "\"";
    if (system(cmd.c_str()) != 0) { err = "The download failed (is curl installed?)"; return false; }
    return true;
#endif
}

bool fetch(const std::string& dir, const std::vector<Model>& list, std::atomic<float>& progress, std::string& status, std::string& err,
           const std::atomic<bool>& cancel) {
    if (!Util::makeDirs(dir)) { err = "Can't make the folder " + dir; return false; }
    long long all = 0, before = 0;
    for (const Model& m : list) all += m.size;
    for (const Model& m : list) {
        status = m.what;
        const bool ok = get(m.url, Util::join(dir, m.file), [&](long long done, long long) {
            progress = all > 0 ? (float)(before + done) / all : 0;
            return !cancel.load();
        }, err);
        if (!ok) return false;
        before += m.size;
    }
    // the licence and the list beside them
    for (const char* f : {"LICENSE-Apache-2.0.txt", "README.md"}) {
        std::error_code ec;
        const std::string src = Util::exeDir() + "models/" + f;
        if (Util::fileExists(src) && !Util::fileExists(Util::join(dir, f))) fs::copy_file(fs::u8path(src), fs::u8path(Util::join(dir, f)), ec);
    }
    progress = 1;
    return true;
}

}  // namespace Download
