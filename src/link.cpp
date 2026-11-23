// Pantomimus - the live link (see link.h)
#include "link.h"
#include "export.h"
#include "util.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define BAD_SOCK INVALID_SOCKET
static void closeSock(Sock s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
typedef int Sock;
#define BAD_SOCK (-1)
static void closeSock(Sock s) { close(s); }
#endif

namespace Link {

static bool netUp() {
#ifdef _WIN32
    static bool ok = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    return ok;
#else
    return true;
#endif
}

static std::string f6(float v) { char b[32]; snprintf(b, sizeof b, "%.6g", v); return b; }

std::string skeletonMessage(const Skel::Skeleton& s, int naming) {
    std::ostringstream o;
    o << "{\"type\":\"skeleton\",\"name\":\"" << s.name << "\",\"bones\":[";
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const Skel::Bone& b = s.bones[i];
        o << (i ? "," : "") << "{\"name\":\"" << Skel::nameOf(s, (int)i, naming) << "\",\"parent\":" << b.parent
          << ",\"head\":[" << f6(b.head.x) << "," << f6(b.head.y) << "," << f6(b.head.z) << "],\"tail\":[" << f6(b.tail.x) << "," << f6(b.tail.y) << "," << f6(b.tail.z) << "]}";
    }
    o << "]}\n";
    return o.str();
}

std::string poseMessage(const Skel::Skeleton& s, const Skel::Pose& p, double t, const bool contact[2]) {
    Skel::World w; Skel::forward(s, p, w);
    std::ostringstream o;
    o << "{\"type\":\"pose\",\"time\":" << f6((float)t) << ",\"bones\":[";
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const M::Q& q = w.rot[i]; const M::V3& h = w.head[i];
        o << (i ? "," : "") << "[" << f6(q.w) << "," << f6(q.x) << "," << f6(q.y) << "," << f6(q.z) << "," << f6(h.x) << "," << f6(h.y) << "," << f6(h.z) << "]";
    }
    o << "],\"contact\":[" << (contact && contact[0] ? 1 : 0) << "," << (contact && contact[1] ? 1 : 0) << "]}\n";
    return o.str();
}

static bool sendAll(Sock s, const std::string& d) {
    size_t at = 0;
    while (at < d.size()) {
        const int n = (int)::send(s, d.data() + at, (int)(d.size() - at), 0);
        if (n <= 0) return false;
        at += n;
    }
    return true;
}

// ---------------------------------------------------------------- the server
struct Server::Impl {
    Sock listener = BAD_SOCK;
    std::thread accepter, sender;
    std::atomic<bool> quit{false};
    std::mutex mu;
    std::condition_variable cv;
    std::vector<Sock> clients, fresh;
    std::string skeleton, latest;
    bool hasLatest = false;
};

bool Server::start(int port, std::string& err) {
    stop();
    if (!netUp()) { err = "No networking"; return false; }
    Sock l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l == BAD_SOCK) { err = "Can't make a socket"; return false; }
    int yes = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (bind(l, (sockaddr*)&a, sizeof a) != 0 || listen(l, 8) != 0) { closeSock(l); err = "Port " + std::to_string(port) + " is taken"; return false; }
    impl_ = new Impl;
    impl_->listener = l;
    port_ = port;
    Impl* im = impl_;
    im->accepter = std::thread([im] {
        while (!im->quit) {
            Sock c = accept(im->listener, nullptr, nullptr);
            if (c == BAD_SOCK) { if (im->quit) break; continue; }
            int one = 1;
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
            std::lock_guard<std::mutex> g(im->mu);
            im->fresh.push_back(c);
            im->cv.notify_one();
        }
    });
    im->sender = std::thread([im] {
        std::unique_lock<std::mutex> g(im->mu);
        while (!im->quit) {
            im->cv.wait(g, [&] { return im->quit || im->hasLatest || !im->fresh.empty(); });
            if (im->quit) break;
            std::vector<Sock> fresh; fresh.swap(im->fresh);
            const std::string sk = im->skeleton, msg = im->hasLatest ? im->latest : std::string();
            im->hasLatest = false;
            std::vector<Sock> cl = im->clients;
            g.unlock();
            std::vector<Sock> keep;
            for (Sock c : fresh) { if (!sk.empty() && sendAll(c, sk)) cl.push_back(c); else if (sk.empty()) cl.push_back(c); else closeSock(c); }
            for (Sock c : cl) { if (msg.empty() || sendAll(c, msg)) keep.push_back(c); else closeSock(c); }
            g.lock();
            im->clients = keep;
        }
    });
    return true;
}

void Server::stop() {
    if (!impl_) return;
    impl_->quit = true;
    closeSock(impl_->listener);
    impl_->cv.notify_all();
    if (impl_->accepter.joinable()) impl_->accepter.join();
    if (impl_->sender.joinable()) impl_->sender.join();
    for (Sock c : impl_->clients) closeSock(c);
    for (Sock c : impl_->fresh) closeSock(c);
    delete impl_;
    impl_ = nullptr;
}

bool Server::running() const { return impl_ != nullptr; }
int Server::clients() const {
    if (!impl_) return 0;
    std::lock_guard<std::mutex> g(impl_->mu);
    return (int)(impl_->clients.size() + impl_->fresh.size());
}

void Server::setSkeleton(const Skel::Skeleton& s, int naming) {
    if (!impl_) return;
    const std::string m = skeletonMessage(s, naming);
    std::lock_guard<std::mutex> g(impl_->mu);
    if (m == impl_->skeleton) return;
    impl_->skeleton = m;
    // the clients already there hear of the new skeleton too
    for (Sock c : impl_->clients) impl_->fresh.push_back(c);
    impl_->clients.clear();
    impl_->cv.notify_one();
}

void Server::send(const Skel::Skeleton& s, const Skel::Pose& p, double t, const bool contact[2]) {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> g(impl_->mu);
        if (impl_->clients.empty() && impl_->fresh.empty()) return;
    }
    const std::string m = poseMessage(s, p, t, contact);
    std::lock_guard<std::mutex> g(impl_->mu);
    impl_->latest = m; impl_->hasLatest = true;
    impl_->cv.notify_one();
}

// ---------------------------------------------------------------- Pygmalion
struct Pygmalion::Impl {
    std::thread worker;
    std::atomic<bool> quit{false};
    std::mutex mu;
    std::condition_variable cv;
    Skel::Skeleton skel;
    Skel::Pose pose;
    bool fresh = false;
    std::string host, status = "Not connected";
    int port = PYGMALION_PORT;
    float rate = 8;
};

static bool call(const std::string& host, int port, const std::string& msg, std::string& reply, std::string& err) {
    if (!netUp()) { err = "No networking"; return false; }
    Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) { err = "No socket"; return false; }
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, host.c_str(), &a.sin_addr);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { closeSock(s); err = "Pygmalion isn't listening (Settings > Socket > Accept connections)"; return false; }
    bool ok = sendAll(s, msg);
    char buf[4096];
    reply.clear();
    for (int i = 0; ok && i < 1000; ++i) {
        const int n = (int)recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        reply.append(buf, n);
        if (reply.size() > (1 << 20)) break;
    }
    closeSock(s);
    if (!ok) err = "Pygmalion closed the connection";
    return ok;
}

void Pygmalion::start(const std::string& host, int port, float rate) {
    stop();
    impl_ = new Impl;
    impl_->host = host; impl_->port = port; impl_->rate = std::max(0.5f, rate);
    Impl* im = impl_;
    im->worker = std::thread([im] {
        const std::string file = Util::join(Util::tempDir(), "pantomimus_live.bvh");
        std::unique_lock<std::mutex> g(im->mu);
        while (!im->quit) {
            im->cv.wait_for(g, std::chrono::milliseconds((int)(1000 / im->rate)), [&] { return im->quit.load(); });
            if (im->quit) break;
            if (!im->fresh) continue;
            const Skel::Skeleton s = im->skel; const Skel::Pose p = im->pose;
            im->fresh = false;
            g.unlock();
            std::string err, reply;
            Export::Options o; o.naming = Skel::NAME_MIXAMO_BARE; o.scale = 1;
            std::string json;
            for (char c : file) { if (c == '\\' || c == '"') json.push_back('\\'); json.push_back(c); }
            std::string st;
            if (!Export::poseBvh(s, p, file, o, err)) st = err;
            else if (!call(im->host, im->port, "{\"function\": \"getPose\", \"params\": {\"poseFilename\": \"" + json + "\"}}", reply, err)) st = err;
            // "No skeleton": Pygmalion has posed the person (its own rig) but has no export skeleton chosen to report
            else st = reply.find("\"error\": \"\"") != std::string::npos || reply.find("\"error\":\"\"") != std::string::npos ||
                              reply.find("No skeleton") != std::string::npos || reply.find("error") == std::string::npos
                          ? "Connected: Pygmalion takes the pose" : "Pygmalion: " + reply.substr(0, 160);
            g.lock();
            im->status = st;
        }
    });
}

void Pygmalion::stop() {
    if (!impl_) return;
    impl_->quit = true;
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    delete impl_;
    impl_ = nullptr;
}
bool Pygmalion::running() const { return impl_ != nullptr; }

void Pygmalion::send(const Skel::Skeleton& s, const Skel::Pose& p) {
    if (!impl_) return;
    std::lock_guard<std::mutex> g(impl_->mu);
    if (impl_->skel.bones.size() != s.bones.size()) impl_->skel = s;
    impl_->pose = p; impl_->fresh = true;
}

std::string Pygmalion::status() const {
    if (!impl_) return "Off";
    std::lock_guard<std::mutex> g(impl_->mu);
    return impl_->status;
}

// ---------------------------------------------------------------- a test client
bool readLine(const std::string& host, int port, std::string& line, int timeoutMs, std::string& err) {
    if (!netUp()) { err = "No networking"; return false; }
    Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, host.c_str(), &a.sin_addr);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { closeSock(s); err = "Can't connect"; return false; }
#ifdef _WIN32
    DWORD tv = (DWORD)timeoutMs;
#else
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    line.clear();
    char c;
    while (true) {
        const int n = (int)recv(s, &c, 1, 0);
        if (n <= 0) { closeSock(s); err = "No line came"; return false; }
        if (c == '\n') break;
        line.push_back(c);
    }
    closeSock(s);
    return true;
}

}  // namespace Link
