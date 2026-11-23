// Pantomimus - the window: Atelier's look (Diorama's style: greys, the Atelier blue, square panels, docked panels with
// header tabs), the controls on the left, the camera and the 3D view in the middle, the timeline under them, the takes
// and landmarks on the right, the status bar at the bottom. A worker thread reads the source, tracks, solves and records.
//
//   Pantomimus [--source picture|video] [--camera N] [--lang xx]
//   Pantomimus --script NAME [--source FILE] [--capture shot.bmp]   a test: drives the window, prints a line, a screenshot, quits
//   Pantomimus --selftest                                           the unit tests
#include "app.h"
#include "download.h"
#include "lang.h"
#include "splash.h"
#include "util.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------- the look (Atelier's DESIGN.md, from Diorama)
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255), C_POPUP = IM_COL32(0x21, 0x21, 0x21, 255),
                   C_WELL = IM_COL32(0x14, 0x14, 0x14, 255), C_RAISED = IM_COL32(0x2e, 0x2e, 0x2e, 255), C_BUTTON = IM_COL32(0x33, 0x33, 0x33, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255), C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255), C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WARN = IM_COL32(0xf0, 0xb0, 0x60, 255),
                   C_ERROR = IM_COL32(0xf0, 0x70, 0x70, 255), C_OK = IM_COL32(0x5a, 0xb0, 0x4a, 255);
// content colours (the performer's sides), not controls
static const ImU32 C_LEFT = IM_COL32(0xdc, 0x9e, 0x5c, 255), C_RIGHT = IM_COL32(0x5c, 0xb3, 0xa8, 255), C_MID = IM_COL32(0xd0, 0xd0, 0xd0, 255);
static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2; s.TabRounding = 2;
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1; s.IndentSpacing = 16;
    s.WindowPadding = ImVec2(10, 10); s.FramePadding = ImVec2(6, 4); s.ItemSpacing = ImVec2(6, 6); s.ScrollbarSize = 10;
    s.DockingSeparatorSize = 2; s.WindowMenuButtonPosition = ImGuiDir_Left;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(C_POPUP);
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE); c[ImGuiCol_SeparatorHovered] = col4(C_DEEP); c[ImGuiCol_SeparatorActive] = col4(C_BLUE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(C_RAISED); c[ImGuiCol_FrameBgActive] = col4(C_RAISED);
    c[ImGuiCol_MenuBarBg] = col4(C_BG); c[ImGuiCol_TitleBg] = col4(C_BG); c[ImGuiCol_TitleBgActive] = col4(C_BG); c[ImGuiCol_TitleBgCollapsed] = col4(C_BG);
    c[ImGuiCol_CheckMark] = col4(C_BLUE); c[ImGuiCol_SliderGrab] = col4(C_BLUE); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(C_BUTTON); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(C_DEEP); c[ImGuiCol_HeaderHovered] = col4(C_BUTTON); c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_Tab] = col4(C_PANEL); c[ImGuiCol_TabActive] = col4(C_DEEP); c[ImGuiCol_TabHovered] = col4(C_BLUE);
    c[ImGuiCol_TabUnfocused] = col4(C_PANEL); c[ImGuiCol_TabUnfocusedActive] = col4(C_DEEP);
    c[ImGuiCol_ResizeGrip] = col4(C_PANEL); c[ImGuiCol_ResizeGripHovered] = col4(C_DEEP); c[ImGuiCol_ResizeGripActive] = col4(C_BLUE);
    c[ImGuiCol_ScrollbarBg] = col4(C_PANEL); c[ImGuiCol_ScrollbarGrab] = col4(C_BUTTON); c[ImGuiCol_ScrollbarGrabHovered] = col4(C_RAISED);
    c[ImGuiCol_ScrollbarGrabActive] = col4(C_BLUE);
    c[ImGuiCol_DragDropTarget] = col4(C_BLUE); c[ImGuiCol_NavHighlight] = col4(C_BLUE);
    c[ImGuiCol_DockingPreview] = col4(C_BLUE); c[ImGuiCol_DockingEmptyBg] = col4(C_BG);
    c[ImGuiCol_PlotHistogram] = col4(C_BLUE); c[ImGuiCol_TextSelectedBg] = col4(C_DEEP);
    c[ImGuiCol_TableHeaderBg] = col4(C_BG); c[ImGuiCol_TableBorderStrong] = col4(C_LINE); c[ImGuiCol_TableBorderLight] = col4(C_LINE);
    c[ImGuiCol_TableRowBg] = col4(C_PANEL); c[ImGuiCol_TableRowBgAlt] = col4(C_PANEL);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

static ImFont *fontBold = nullptr, *fontMono = nullptr;

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
#ifdef _WIN32
    const char* ui = "C:/Windows/Fonts/segoeui.ttf";
    const char* bold = "C:/Windows/Fonts/segoeuib.ttf";
    const char* light = "C:/Windows/Fonts/segoeuil.ttf";
    const char* mono = "C:/Windows/Fonts/consola.ttf";
    const char* sym = "C:/Windows/Fonts/seguisym.ttf";
#else
    const char* ui = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const char* bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    const char* light = ui;
    const char* mono = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
    const char* sym = ui;
#endif
    if (!Util::fileExists(ui)) { io.Fonts->AddFontDefault(); Splash::setFonts(nullptr, nullptr); return; }
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x0300, 0x036F, 0x0400, 0x04FF, 0x2010, 0x2027, 0x2030, 0x205E, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    static const ImWchar symbols[] = {0x2190, 0x21FF, 0x23E9, 0x23FA, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0};
    const std::string lang = Lang::current();
    auto add = [&](const char* file, float size) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(Util::fileExists(file) ? file : ui, size, nullptr, ranges);
        ImFontConfig m; m.MergeMode = true;
        if (Util::fileExists(sym)) io.Fonts->AddFontFromFileTTF(sym, size, &m, symbols);
#ifdef _WIN32
        if (Lang::needsCJK()) {
            const char* cjk = lang == "ko" ? "C:/Windows/Fonts/malgun.ttf" : lang == "zh" ? "C:/Windows/Fonts/msyh.ttc" : "C:/Windows/Fonts/YuGothM.ttc";
            const ImWchar* r = lang == "ko" ? io.Fonts->GetGlyphRangesKorean() : lang == "zh" ? io.Fonts->GetGlyphRangesChineseSimplifiedCommon() : io.Fonts->GetGlyphRangesJapanese();
            if (Util::fileExists(cjk)) io.Fonts->AddFontFromFileTTF(cjk, size, &m, r);
        }
#endif
        return f;
    };
    add(ui, 16.0f);
    fontBold = add(bold, 16.0f);
    fontMono = add(mono, 16.0f);
    ImFont* title = add(light, 46.0f);
    ImFont* smallF = add(ui, 13.0f);
    Splash::setFonts(title, smallF);
}

namespace App {

State st;
static const Settings DEF;   // the defaults (right-click on a slider)

std::string State::modelsDir() const { return set.modelsDir.empty() ? Util::defaultModelsDir(Pose::DETECTOR_FILE) : set.modelsDir; }

static std::string settingsFile() { return Util::join(Util::configDir(), "settings.ini"); }
static std::string layoutFile() { return Util::join(Util::configDir(), "layout.ini"); }

void say(const std::string& m) { st.status = m; st.statusTime = Util::now(); }
#ifdef _WIN32
HANDLE outHandle = nullptr;   // where the tests' lines go (see attachConsole)
#endif
void out(const std::string& line) {
#ifdef _WIN32
    if (outHandle && outHandle != INVALID_HANDLE_VALUE) {
        const std::string l = line + "\r\n";
        DWORD n = 0;
        WriteFile(outHandle, l.data(), (DWORD)l.size(), &n, nullptr);
        return;
    }
#endif
    fprintf(stdout, "%s\n", line.c_str());
    fflush(stdout);
}

// ---------------------------------------------------------------- settings.ini
void saveSettings() {
    std::ofstream f(fs::u8path(settingsFile()));
    const Settings& s = st.set;
    auto b = [](bool v) { return v ? 1 : 0; };
    f << "language=" << s.language << "\nmodelsDir=" << s.modelsDir << "\nopenLast=" << b(s.openLast) << "\ncameraAtStart=" << b(s.cameraAtStart)
      << "\nlastProject=" << s.lastProject << "\ncamera=" << s.camera << "\ncamW=" << s.camW << "\ncamH=" << s.camH << "\ncamFps=" << s.camFps
      << "\nmirrorView=" << b(s.mirrorView) << "\nloopVideo=" << b(s.loopVideo) << "\noverlay=" << b(s.overlay) << "\noverlayRoi=" << b(s.overlayRoi) << "\noverlayLabels=" << b(s.overlayLabels)
      << "\nmodel=" << s.pose.model << "\ndetectScore=" << s.pose.detectScore << "\npresence=" << s.pose.presence << "\nroiScale=" << s.pose.roiScale
      << "\nrefine=" << b(s.pose.refine) << "\nthreads=" << s.pose.threads
      << "\nfilterOn=" << b(s.filter.on) << "\nminCutoff=" << s.filter.minCutoff << "\nbeta=" << s.filter.beta << "\ndCutoff=" << s.filter.dCutoff
      << "\nworldMinCutoff=" << s.filter.worldMinCutoff << "\nworldBeta=" << s.filter.worldBeta << "\nworldDCutoff=" << s.filter.worldDCutoff
      << "\nvisibility=" << s.filter.visibility << "\nhold=" << s.filter.holdSeconds
      << "\nmirror=" << b(s.solver.mirror) << "\nrootMode=" << s.solver.rootMode << "\ngroundLock=" << b(s.solver.groundLock) << "\nrootScale=" << s.solver.rootScale
      << "\nfov=" << s.solver.fov << "\nheadPitch=" << s.solver.headPitch << "\nshrug=" << s.solver.shrug << "\nspineBend=" << s.solver.spineBend
      << "\nforearmTwist=" << s.solver.forearmTwist << "\nlimbVisibility=" << s.solver.visibility << "\nunseen=" << s.solver.unseen
      << "\nlegs=" << b(s.solver.legs) << "\narms=" << b(s.solver.arms) << "\nhead=" << b(s.solver.head) << "\ntorso=" << b(s.solver.torso)
      << "\ncontactHeight=" << s.solver.contactHeight << "\ncontactSpeed=" << s.solver.contactSpeed << "\nfootLock=" << b(s.solver.footLock)
      << "\nhands=" << b(s.hands.on) << "\nhandPresence=" << s.hands.presence << "\nhandRoi=" << s.hands.roiScale << "\ncurl=" << s.hands.curl
      << "\nspread=" << s.hands.spread << "\nhandSmooth=" << s.hands.smooth
      << "\nface=" << b(s.face.on) << "\nfaceDetect=" << s.face.detectScore << "\nfacePresence=" << s.face.presence << "\nfaceRoi=" << s.face.roiScale
      << "\nfaceSmooth=" << s.face.smooth << "\nweightSmooth=" << s.face.weightSmooth << "\nfaceGain=" << s.face.gain << "\neyesFromIris=" << b(s.face.eyesFromIris)
      << "\nfaceFromBody=" << b(s.face.fromBody) << "\nmarkers=" << b(s.markers.on) << "\nmarkerGuide=" << b(s.markerGuide) << "\nmarkerDark=" << s.markers.darkness
      << "\nmarkerMin=" << s.markers.minDot << "\nmarkerMax=" << s.markers.maxDot << "\nmarkerSearch=" << s.markers.search << "\nmarkerBlend=" << s.markers.blend << "\nmarkerRound=" << s.markers.roundness
      << "\nfps=" << s.cleanup.fps << "\nsmooth=" << s.cleanup.smooth << "\ncleanFootLock=" << b(s.cleanup.footLock) << "\nminPlant=" << s.cleanup.minPlant
      << "\nlockBlend=" << s.cleanup.lockBlend << "\nloop=" << b(s.cleanup.loop) << "\nloopFrames=" << s.cleanup.loopFrames << "\ninPlace=" << b(s.cleanup.inPlace)
      << "\nstartAtOrigin=" << b(s.cleanup.startAtOrigin)
      << "\nexportFormat=" << s.exportFormat << "\nnaming=" << s.exportOpts.naming << "\nexportScale=" << s.exportOpts.scale
      << "\nmannequin=" << b(s.exportOpts.mannequin) << "\nzUp=" << s.exportOpts.zUp
      << "\ntPose=" << b(s.tPose) << "\nheight=" << s.height << "\ncountdown=" << s.countdown
      << "\nlinkPort=" << s.linkPort << "\npygHost=" << s.pygHost << "\npygPort=" << s.pygPort << "\npygRate=" << s.pygRate
      << "\nlookMannequin=" << b(s.look.mannequin) << "\nlookBones=" << b(s.look.bones) << "\nlookFloor=" << b(s.look.floor)
      << "\nlookLandmarks=" << b(s.look.landmarks) << "\nlookFollow=" << b(s.look.follow) << "\nlookFace=" << b(s.look.face) << "\n";
}

static void loadSettings() {
    std::ifstream f(fs::u8path(settingsFile()));
    std::string line;
    Settings& s = st.set;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = Util::trim(line.substr(eq + 1));
        const float x = (float)atof(v.c_str());
        const int i = atoi(v.c_str());
        const bool bv = i != 0;
        static const std::map<std::string, float*> floats = {
            {"camFps", &s.camFps}, {"detectScore", &s.pose.detectScore}, {"presence", &s.pose.presence}, {"roiScale", &s.pose.roiScale},
            {"minCutoff", &s.filter.minCutoff}, {"beta", &s.filter.beta}, {"dCutoff", &s.filter.dCutoff}, {"worldMinCutoff", &s.filter.worldMinCutoff},
            {"worldBeta", &s.filter.worldBeta}, {"worldDCutoff", &s.filter.worldDCutoff}, {"visibility", &s.filter.visibility}, {"hold", &s.filter.holdSeconds},
            {"rootScale", &s.solver.rootScale}, {"fov", &s.solver.fov}, {"headPitch", &s.solver.headPitch}, {"shrug", &s.solver.shrug},
            {"spineBend", &s.solver.spineBend}, {"forearmTwist", &s.solver.forearmTwist}, {"limbVisibility", &s.solver.visibility},
            {"contactHeight", &s.solver.contactHeight}, {"contactSpeed", &s.solver.contactSpeed}, {"handPresence", &s.hands.presence},
            {"handRoi", &s.hands.roiScale}, {"curl", &s.hands.curl}, {"spread", &s.hands.spread}, {"handSmooth", &s.hands.smooth},
            {"faceDetect", &s.face.detectScore}, {"facePresence", &s.face.presence}, {"faceRoi", &s.face.roiScale}, {"faceSmooth", &s.face.smooth},
            {"weightSmooth", &s.face.weightSmooth}, {"faceGain", &s.face.gain}, {"markerDark", &s.markers.darkness}, {"markerMin", &s.markers.minDot},
            {"markerMax", &s.markers.maxDot}, {"markerSearch", &s.markers.search}, {"markerBlend", &s.markers.blend}, {"markerRound", &s.markers.roundness},
            {"fps", &s.cleanup.fps}, {"smooth", &s.cleanup.smooth}, {"lockBlend", &s.cleanup.lockBlend}, {"loopFrames", &s.cleanup.loopFrames},
            {"exportScale", &s.exportOpts.scale}, {"height", &s.height}, {"pygRate", &s.pygRate}};
        static const std::map<std::string, int*> ints = {
            {"camera", &s.camera}, {"camW", &s.camW}, {"camH", &s.camH}, {"model", &s.pose.model}, {"threads", &s.pose.threads},
            {"rootMode", &s.solver.rootMode}, {"unseen", &s.solver.unseen}, {"minPlant", &s.cleanup.minPlant}, {"exportFormat", &s.exportFormat},
            {"naming", &s.exportOpts.naming}, {"zUp", &s.exportOpts.zUp}, {"countdown", &s.countdown}, {"linkPort", &s.linkPort}, {"pygPort", &s.pygPort}};
        static const std::map<std::string, bool*> bools = {
            {"openLast", &s.openLast}, {"cameraAtStart", &s.cameraAtStart}, {"mirrorView", &s.mirrorView}, {"loopVideo", &s.loopVideo}, {"overlay", &s.overlay},
            {"overlayRoi", &s.overlayRoi}, {"overlayLabels", &s.overlayLabels}, {"refine", &s.pose.refine}, {"filterOn", &s.filter.on},
            {"mirror", &s.solver.mirror}, {"groundLock", &s.solver.groundLock}, {"legs", &s.solver.legs}, {"arms", &s.solver.arms},
            {"head", &s.solver.head}, {"torso", &s.solver.torso}, {"footLock", &s.solver.footLock}, {"hands", &s.hands.on},
            {"face", &s.face.on}, {"eyesFromIris", &s.face.eyesFromIris}, {"faceFromBody", &s.face.fromBody}, {"markers", &s.markers.on}, {"markerGuide", &s.markerGuide},
            {"cleanFootLock", &s.cleanup.footLock}, {"loop", &s.cleanup.loop}, {"inPlace", &s.cleanup.inPlace}, {"startAtOrigin", &s.cleanup.startAtOrigin},
            {"mannequin", &s.exportOpts.mannequin}, {"tPose", &s.tPose}, {"lookMannequin", &s.look.mannequin}, {"lookBones", &s.look.bones},
            {"lookFloor", &s.look.floor}, {"lookLandmarks", &s.look.landmarks}, {"lookFollow", &s.look.follow}, {"lookFace", &s.look.face}};
        if (auto it = floats.find(k); it != floats.end()) *it->second = x;
        else if (auto it2 = ints.find(k); it2 != ints.end()) *it2->second = i;
        else if (auto it3 = bools.find(k); it3 != bools.end()) *it3->second = bv;
        else if (k == "language") s.language = v;
        else if (k == "modelsDir") s.modelsDir = v;
        else if (k == "lastProject") s.lastProject = v;
        else if (k == "pygHost") s.pygHost = v;
    }
    s.pose.model = std::clamp(s.pose.model, 0, 2);
    s.exportFormat = std::clamp(s.exportFormat, 0, 2);
    s.height = std::clamp(s.height, 0.5f, 3.0f);
}

// ---------------------------------------------------------------- the worker
namespace {
struct Request { int kind = 0; int camera = 0; std::string path; bool trackAll = false; };   // kind: 1 camera, 2 file, 3 close
std::thread worker;
std::atomic<bool> quitting{false};
std::mutex mu;                 // guards everything below
Request request;
Settings workerSet;            // the settings the worker uses (copied when they change)
Skel::Skeleton workerSkel;
bool settingsNew = true, skeletonNew = true, resetNew = false;
bool calibrateNew = false, clearMarkersNew = false;
Live latest;
bool rec = false, trackingAll = false;
bool videoDone = false, replayReq = false;   // a watched video has reached its end; a replay was asked for
double recStart = -1, countdownUntil = 0;
Clip::Take recTake;
bool sourceIsOpen = false;
Link::Server server;
Link::Pygmalion pyg;
std::atomic<float> dlProgress{-1};
std::string dlStatus;
std::atomic<bool> dlCancel{false};
std::thread downloader;
}  // namespace

static void finishRecording() {   // under mu
    rec = false;
    trackingAll = false;
    if (recTake.raw.size() < 2) { recTake = Clip::Take(); return; }
    // the take: times from its first frame
    const double t0 = recTake.t.front();
    for (double& t : recTake.t) t -= t0;
    int n = 1;
    for (const auto& t : st.takes) if (t.name.rfind("Take ", 0) == 0) n = std::max(n, atoi(t.name.c_str() + 5) + 1);
    if (recTake.name.empty()) recTake.name = "Take " + std::to_string(n);
    st.takes.push_back(std::move(recTake));
    recTake = Clip::Take();
    st.sel = (int)st.takes.size() - 1;
    st.bakedDirty = true; st.showTake = true; st.playhead = 0; st.modified = true;
}

static void work() {
    std::unique_ptr<Src::Source> src;
    Pose::Tracker tracker;
    Hands::Tracker handTracker;
    Face::Tracker faceTracker;
    Markers::Tracker markerTracker;
    Filter::Landmarks filt;
    Solver::Solver solver;
    Settings set; Skel::Skeleton skel;
    std::string loadedDir, curFile; int loadedModel = -1, loadedThreads = -1;
    std::deque<double> stamps;
    double lastPlay = 0, lastT = -1;
    long long frameNo = 0;
    Image frame;
    while (!quitting) {
        Request req;
        {
            std::lock_guard<std::mutex> g(mu);
            req = request; request = Request();
            if (settingsNew) { set = workerSet; settingsNew = false; }
            if (skeletonNew) { skel = workerSkel; skeletonNew = false; solver.reset(); }
            if (resetNew) { tracker.reset(); filt.reset(); solver.reset(); faceTracker.reset(); resetNew = false; }
            if (clearMarkersNew) { markerTracker.clear(); clearMarkersNew = false; }
        }
        if (req.kind) {
            src.reset();
            std::string err;
            if (req.kind == 1) src = Src::openCamera(req.camera, set.camW, set.camH, set.camFps, err);
            else if (req.kind == 2) src = Src::openFile(req.path, err);
            curFile = req.kind == 2 ? req.path : std::string();
            tracker.reset(); filt.reset(); solver.reset(); faceTracker.reset(); stamps.clear(); lastT = -1;
            std::lock_guard<std::mutex> g(mu);
            videoDone = false; replayReq = false;
            latest = Live();
            latest.error = req.kind != 3 && !src ? err : "";
            latest.source = src ? src->describe() : "";
            if (src && src->isFile()) latest.videoLength = src->duration();
            sourceIsOpen = src != nullptr;
            if (src && req.trackAll) {
                rec = true; trackingAll = true; recTake = Clip::Take();
                recTake.name = Util::stem(req.path); recTake.source = req.path;
            }
            if (!src && rec) finishRecording();
        }
        // the models, when the folder or the choice changed
        const std::string dir = st.modelsDir();
        if (dir != loadedDir || set.pose.model != loadedModel || set.pose.threads != loadedThreads) {
            std::string err;
            if (Util::fileExists(Util::join(dir, Pose::DETECTOR_FILE)) && Util::fileExists(Util::join(dir, Pose::modelFile(set.pose.model)))) {
                if (tracker.load(dir, set.pose, err)) { loadedDir = dir; loadedModel = set.pose.model; loadedThreads = set.pose.threads; }
                else { std::lock_guard<std::mutex> g(mu); latest.error = err; loadedDir = dir; loadedModel = set.pose.model; loadedThreads = set.pose.threads; }
            }
        }
        if (set.hands.on && !handTracker.loaded() && Util::fileExists(Util::join(dir, Hands::LANDMARK_FILE))) {
            std::string err;
            if (!handTracker.load(dir, set.pose.threads, err)) { std::lock_guard<std::mutex> g(mu); latest.error = err; }
        }
        if (set.face.on && !faceTracker.loaded() && Util::fileExists(Util::join(dir, Face::LANDMARK_FILE)) &&
            Util::fileExists(Util::join(dir, Face::BLENDSHAPE_FILE)) && Util::fileExists(Util::join(dir, Face::DETECTOR_FILE))) {
            std::string err;
            if (!faceTracker.load(dir, set.pose.threads, err)) { std::lock_guard<std::mutex> g(mu); latest.error = err; }
        }
        if (!src) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); continue; }
        // a watched video that reached its end waits on its last frame until the user replays it (or looping is on)
        {
            bool done; { std::lock_guard<std::mutex> g(mu); done = videoDone; }
            if (done) {
                bool want; { std::lock_guard<std::mutex> g(mu); want = replayReq || set.loopVideo; replayReq = false; }
                if (!want) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); continue; }
                bool back = src->seek(0);
                if (!back && !curFile.empty()) { std::string e; auto s2 = Src::openFile(curFile, e); if (s2) { src = std::move(s2); back = true; } }
                tracker.reset(); filt.reset(); solver.reset(); faceTracker.reset(); lastT = -1;
                std::lock_guard<std::mutex> g(mu);
                videoDone = !back; latest.ended = !back;
                if (back) continue;   // next loop reads the first frame
            }
        }
        double t = 0;
        std::string err;
        if (!src->read(frame, t, err)) {
            if (err == "end") {
                bool all; { std::lock_guard<std::mutex> g(mu); all = trackingAll; }
                std::lock_guard<std::mutex> g(mu);
                latest.ended = true;
                if (all) { finishRecording(); src.reset(); sourceIsOpen = false; }
                else videoDone = true;   // keep the source so it can be played again or looped
                continue;
            }
            std::lock_guard<std::mutex> g(mu);
            latest.error = err;
            src.reset();
            sourceIsOpen = false;
            continue;
        }
        // a video watched (not tracked whole) plays at its own pace
        if (src->isFile()) {
            bool all; { std::lock_guard<std::mutex> g(mu); all = trackingAll; }
            if (!all && lastT >= 0) {
                const double wait = (t - lastT) - (Util::now() - lastPlay);
                if (wait > 0 && wait < 1) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
            }
            lastPlay = Util::now(); lastT = t;
        }
        Pose::Result raw;
        if (tracker.loaded() && !tracker.process(frame, set.pose, raw, err)) { std::lock_guard<std::mutex> g(mu); latest.error = err; }
        Pose::Result filtered = raw;
        filt.apply(filtered, t, set.filter);
        Solver::Frame solved;
        if (!skel.empty()) solver.solve(skel, filtered, t, set.solver, solved);
        Hands::Result hands;
        if (set.hands.on && handTracker.loaded() && filtered.found) {
            if (!handTracker.process(frame, filtered, set.hands, hands, err)) { std::lock_guard<std::mutex> g(mu); latest.error = err; }
            else if (!skel.empty()) Hands::solveFingers(skel, hands, set.solver.mirror, set.hands, solved.pose);
        }
        // the face: from the body's face when the body is tracked, else found on its own (a close-up); the Sharpie
        // markers correct its mesh before the blendshapes are read, and a calibration asked for takes this frame's face
        Face::Result face;
        Markers::Result marks;
        if (set.face.on && faceTracker.loaded()) {
            bool calibrate = false;
            { std::lock_guard<std::mutex> g(mu); calibrate = calibrateNew; calibrateNew = false; }
            std::function<void(Face::Result&)> adjust;
            if (set.markers.on)
                adjust = [&](Face::Result& f) {
                    if (calibrate) markerTracker.calibrate(frame, f, set.markers);
                    markerTracker.track(frame, f, set.markers, marks);
                };
            if (!faceTracker.process(frame, filtered.found ? &filtered : nullptr, set.face, face, err, adjust)) { std::lock_guard<std::mutex> g(mu); latest.error = err; }
        }
        const double now = Util::now();
        stamps.push_back(now);
        while (stamps.size() > 1 && now - stamps.front() > 1.0) stamps.pop_front();
        if (server.running() && !skel.empty() && (filtered.found || face.found)) server.send(skel, solved.pose, t, solved.contact, set.face.on ? &face : nullptr);
        if (pyg.running() && !skel.empty() && filtered.found) pyg.send(skel, solved.pose);
        std::lock_guard<std::mutex> g(mu);
        if (rec) {
            if (recTake.raw.empty()) recStart = t;
            recTake.t.push_back(t - recStart);
            recTake.raw.push_back(raw);
            if (set.hands.on) recTake.hands.push_back(hands);
            if (set.face.on) recTake.faces.push_back(face);
            if (set.face.on && set.markers.on) { Markers::Result m = marks; m.blobs.clear(); recTake.markers.push_back(m); }
            if (recTake.source.empty()) recTake.source = latest.source;
        }
        latest.frame = frame;   // copied: the next read fills `frame`
        latest.frameNo = ++frameNo;
        latest.t = t;
        latest.raw = raw; latest.filtered = filtered; latest.solved = solved; latest.hands = hands; latest.face = face; latest.markers = marks;
        latest.fps = stamps.size() > 1 ? (float)((stamps.size() - 1) / std::max(1e-3, stamps.back() - stamps.front())) : 0;
        latest.ms = raw.ms;
        latest.videoPos = src->isFile() ? t : 0;
        if (src->isFile()) latest.videoLength = src->duration();
        latest.error.clear();
    }
}

void openCamera(int index) { std::lock_guard<std::mutex> g(mu); request = Request(); request.kind = 1; request.camera = index; st.set.camera = index; }
void openFile(const std::string& path) { std::lock_guard<std::mutex> g(mu); request = Request(); request.kind = 2; request.path = path; }
void trackVideo(const std::string& path) { std::lock_guard<std::mutex> g(mu); request = Request(); request.kind = 2; request.path = path; request.trackAll = true; }
void closeSource() { std::lock_guard<std::mutex> g(mu); request = Request(); request.kind = 3; }
bool sourceOpen() { std::lock_guard<std::mutex> g(mu); return sourceIsOpen; }
Live live() { std::lock_guard<std::mutex> g(mu); return latest; }
bool recording() { std::lock_guard<std::mutex> g(mu); return rec || countdownUntil > Util::now(); }
float countdownLeft() { std::lock_guard<std::mutex> g(mu); return (float)std::max(0.0, countdownUntil - Util::now()); }
void startRecording(bool countdown) {
    std::lock_guard<std::mutex> g(mu);
    if (rec) return;
    if (countdown && st.set.countdown > 0) { countdownUntil = Util::now() + st.set.countdown; return; }
    rec = true; recTake = Clip::Take(); recStart = -1;
}
void stopRecording() {
    std::lock_guard<std::mutex> g(mu);
    countdownUntil = 0;
    if (!rec) return;
    if (!recTake.raw.empty()) {
        // the undo step for the new take
    }
    finishRecording();
}
static void tickCountdown() {
    std::lock_guard<std::mutex> g(mu);
    if (countdownUntil > 0 && Util::now() >= countdownUntil) { countdownUntil = 0; rec = true; recTake = Clip::Take(); recStart = -1; }
}
void resetTracking() { std::lock_guard<std::mutex> g(mu); resetNew = true; }
void replayVideo() { std::lock_guard<std::mutex> g(mu); if (videoDone) replayReq = true; }
bool videoEnded() { std::lock_guard<std::mutex> g(mu); return videoDone; }
void calibrateMarkers() { std::lock_guard<std::mutex> g(mu); calibrateNew = true; }
void clearMarkers() { std::lock_guard<std::mutex> g(mu); clearMarkersNew = true; }
void settingsChanged() { std::lock_guard<std::mutex> g(mu); workerSet = st.set; settingsNew = true; st.bakedDirty = true; }
void skeletonChanged() {
    Skel::Skeleton s = st.set.tPose ? Skel::toTPose(st.measured) : st.measured;
    const float h = s.height();
    if (h > 0.1f) s = Skel::scaled(s, st.set.height / h);
    s.name = st.measured.name;
    st.skel = s;
    std::lock_guard<std::mutex> g(mu);
    workerSkel = s; skeletonNew = true; st.bakedDirty = true;
    if (server.running()) server.setSkeleton(s, st.set.exportOpts.naming);
}

// ---------------------------------------------------------------- takes and the project
namespace {
struct Snapshot { std::vector<Clip::Take> takes; int sel; std::string what; };
std::vector<Snapshot> undoStack, redoStack;
}
void pushUndo(const char* what) {
    undoStack.push_back({st.takes, st.sel, what});
    if (undoStack.size() > 40) undoStack.erase(undoStack.begin());
    redoStack.clear();
    st.modified = true;
}
void undo() {
    if (undoStack.empty()) return;
    redoStack.push_back({st.takes, st.sel, undoStack.back().what});
    st.takes = undoStack.back().takes; st.sel = undoStack.back().sel;
    say(std::string(T("Undone: ")) + T(undoStack.back().what.c_str()));
    undoStack.pop_back();
    st.bakedDirty = true;
}
void redo() {
    if (redoStack.empty()) return;
    undoStack.push_back({st.takes, st.sel, redoStack.back().what});
    st.takes = redoStack.back().takes; st.sel = redoStack.back().sel;
    say(std::string(T("Redone: ")) + T(redoStack.back().what.c_str()));
    redoStack.pop_back();
    st.bakedDirty = true;
}
void select(int i) {
    if (i == st.sel) return;
    st.sel = (i >= 0 && i < (int)st.takes.size()) ? i : -1;
    st.bakedDirty = true; st.playhead = 0; st.showTake = st.sel >= 0;
}
void deleteTake(int i) {
    if (i < 0 || i >= (int)st.takes.size()) return;
    pushUndo("Delete take");
    st.takes.erase(st.takes.begin() + i);
    if (st.sel >= (int)st.takes.size()) st.sel = (int)st.takes.size() - 1;
    st.bakedDirty = true;
}
void duplicateTake(int i) {
    if (i < 0 || i >= (int)st.takes.size()) return;
    pushUndo("Duplicate take");
    Clip::Take t = st.takes[i];
    t.name += " (2)";
    st.takes.insert(st.takes.begin() + i + 1, t);
    st.sel = i + 1; st.bakedDirty = true;
}
void rebake() {
    if (!st.bakedDirty) return;
    st.bakedDirty = false;
    if (st.sel < 0 || st.sel >= (int)st.takes.size()) { st.baked = Clip::Baked(); return; }
    Clip::bake(st.takes[st.sel], st.skel, st.set.filter, st.set.solver, st.set.hands, st.set.cleanup, st.baked);
    st.playhead = std::min(st.playhead, st.baked.duration());
}
bool exportTake(int i, const std::string& path, std::string& err) {
    if (i < 0 || i >= (int)st.takes.size()) { err = T("No take to export"); return false; }
    Clip::Baked b;
    Clip::bake(st.takes[i], st.skel, st.set.filter, st.set.solver, st.set.hands, st.set.cleanup, b);
    Export::Options o = st.set.exportOpts;
    o.takeName = st.takes[i].name;
    return Export::save(st.skel, b, path, o, err);
}
bool saveProject(const std::string& path, std::string& err) {
    if (!Clip::saveProject(path, st.takes, err)) return false;
    st.project = path; st.modified = false; st.set.lastProject = path;
    return true;
}
bool openProject(const std::string& path, std::string& err) {
    std::vector<Clip::Take> t;
    if (!Clip::loadProject(path, t, err)) return false;
    st.takes = std::move(t); st.sel = st.takes.empty() ? -1 : 0;
    st.project = path; st.modified = false; st.set.lastProject = path;
    undoStack.clear(); redoStack.clear();
    st.bakedDirty = true; st.playhead = 0; st.showTake = st.sel >= 0;
    return true;
}
void newProject() {
    st.takes.clear(); st.sel = -1; st.project.clear(); st.modified = false;
    undoStack.clear(); redoStack.clear(); st.bakedDirty = true; st.showTake = false;
}

void setLink(bool on) {
    st.set.linkOn = on;
    if (!on) { server.stop(); return; }
    std::string err;
    if (server.start(st.set.linkPort, err)) server.setSkeleton(st.skel, st.set.exportOpts.naming);
    else { say(err); st.set.linkOn = false; }
}
void setPygmalion(bool on) {
    st.set.pygOn = on;
    if (on) pyg.start(st.set.pygHost, st.set.pygPort, st.set.pygRate); else pyg.stop();
}
int linkClients() { return server.clients(); }

void setLanguage(const std::string& code) {
    Lang::load(Util::exeDir() + "lang", code);
    st.set.language = Lang::current();
}

// ---------------------------------------------------------------- models
std::vector<std::string> missingModels() {
    std::vector<std::string> r;
    for (const auto& m : Download::missing(st.modelsDir(), st.set.pose.model, st.set.hands.on, st.set.face.on)) r.push_back(m.file);
    return r;
}
void downloadModels() {
    if (dlProgress >= 0) return;
    if (downloader.joinable()) downloader.join();
    dlProgress = 0; dlCancel = false;
    const std::string dir = st.modelsDir();
    const auto list = Download::missing(dir, st.set.pose.model, st.set.hands.on, st.set.face.on);
    downloader = std::thread([dir, list] {
        std::string status, err;
        const bool ok = Download::fetch(dir, list, dlProgress, status, err, dlCancel);
        { std::lock_guard<std::mutex> g(mu); dlStatus = ok ? std::string(T("The models are in: ")) + dir : std::string(T("The download failed: ")) + err; }
        dlProgress = -1;
        settingsChanged();
    });
}
float downloadProgress() { return dlProgress; }
std::string downloadStatus() { std::lock_guard<std::mutex> g(mu); return dlStatus; }

}  // namespace App

using namespace App;

// ---------------------------------------------------------------- widgets
static bool changed = false;   // a setting changed this frame (the worker gets the new ones)

static void tip(const char* t) { if (t && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", T(t)); }
// a slider with its name and value written on it; right-click resets it
static bool sliderF(const char* label, float* v, float lo, float hi, const char* fmt, float def, const char* tipText = nullptr, ImGuiSliderFlags fl = 0) {
    ImGui::SetNextItemWidth(-1);
    const std::string f = std::string(T(label)) + ": " + fmt;
    bool c = ImGui::SliderFloat((std::string("##") + label).c_str(), v, lo, hi, f.c_str(), fl | ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *v = def; c = true; }
    tip(tipText);
    if (c) changed = true;
    return c;
}
static bool sliderI(const char* label, int* v, int lo, int hi, const char* fmt, int def, const char* tipText = nullptr) {
    ImGui::SetNextItemWidth(-1);
    const std::string f = std::string(T(label)) + ": " + fmt;
    bool c = ImGui::SliderInt((std::string("##") + label).c_str(), v, lo, hi, f.c_str(), ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *v = def; c = true; }
    tip(tipText);
    if (c) changed = true;
    return c;
}
// a choice as a slider: its name and the chosen option's name written on it
static bool sliderChoice(const char* label, int* v, const std::vector<const char*>& names, int def, const char* tipText = nullptr) {
    ImGui::SetNextItemWidth(-1);
    *v = std::clamp(*v, 0, (int)names.size() - 1);
    const std::string f = std::string(T(label)) + ": " + T(names[*v]);
    bool c = ImGui::SliderInt((std::string("##") + label).c_str(), v, 0, (int)names.size() - 1, f.c_str(), ImGuiSliderFlags_NoInput);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *v = def; c = true; }
    tip(tipText);
    if (c) changed = true;
    return c;
}
static bool check(const char* label, bool* v, const char* tipText = nullptr) {
    const bool c = ImGui::Checkbox(T(label), v);
    tip(tipText);
    if (c) changed = true;
    return c;
}
static bool button(const char* label, float w = 0, const char* tipText = nullptr) {
    const bool c = ImGui::Button(T(label), ImVec2(w, 0));
    tip(tipText);
    return c;
}
static bool section(const char* name, bool open = true) {
    ImGui::PushFont(fontBold);
    const bool r = ImGui::CollapsingHeader(T(name), open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopFont();
    return r;
}
static std::string clock(double s) {
    char b[32];
    const int m = (int)(s / 60);
    snprintf(b, sizeof b, "%02d:%05.2f", m, s - m * 60);
    return b;
}

// ---------------------------------------------------------------- the panels
static std::vector<Src::Device> cams;
static bool camsListed = false;
static std::string exportsDir() { std::string d = Util::join(Util::documentsDir(), "Exports"); Util::makeDirs(d); return d; }
static const char* EXT[3] = {".fbx", ".glb", ".bvh"};

static void exportDialog(int take) {
    if (take < 0) { say(T("Record or open a take first")); return; }
    const std::string ext = EXT[st.set.exportFormat];
    const std::string name = Util::join(exportsDir(), st.takes[take].name + ext);
    const char* kinds[3] = {"Filmbox", "glTF binary", "Biovision"};
    const std::string p = Util::saveFile(T("Export the take"), name, {{kinds[st.set.exportFormat], "*" + ext}});
    if (p.empty()) return;
    std::string err;
    if (exportTake(take, p, err)) say(std::string(T("Exported: ")) + p); else say(err);
}

static void controlsPanel() {
    Settings& s = st.set;
    if (section("Source")) {
        if (!camsListed) { cams = Src::cameras(); camsListed = true; }
        std::vector<std::string> names;
        for (const auto& c : cams) names.push_back(c.name);
        ImGui::SetNextItemWidth(-1);
        const std::string cur = cams.empty() ? std::string(T("No camera found")) : names[std::clamp(s.camera, 0, (int)names.size() - 1)];
        if (ImGui::BeginCombo("##camera", cur.c_str())) {
            for (size_t i = 0; i < names.size(); ++i) if (ImGui::Selectable(names[i].c_str(), (int)i == s.camera)) { s.camera = (int)i; changed = true; if (sourceOpen()) openCamera(s.camera); }
            if (ImGui::Selectable(T("Look for cameras again"))) camsListed = false;
            ImGui::EndCombo();
        }
        if (!cams.empty() && s.camera < (int)cams.size()) {
            // the camera's modes as a slider
            const auto& fm = cams[s.camera].formats;
            if (!fm.empty()) {
                int m = 0; double best = 1e18;
                for (size_t i = 0; i < fm.size(); ++i) { const double d = std::fabs(fm[i].w - s.camW) + std::fabs(fm[i].h - s.camH) + std::fabs(fm[i].fps - s.camFps) * 10; if (d < best) { best = d; m = (int)i; } }
                char f[96]; snprintf(f, sizeof f, "%s: %d x %d, %.0f fps", T("Mode"), fm[m].w, fm[m].h, fm[m].fps);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##mode", &m, 0, (int)fm.size() - 1, f, ImGuiSliderFlags_NoInput)) {
                    s.camW = fm[m].w; s.camH = fm[m].h; s.camFps = fm[m].fps; changed = true;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && sourceOpen()) openCamera(s.camera);
                tip("The camera's resolution and frame rate");
            }
        }
        const float w2 = (ImGui::GetContentRegionAvail().x - 6) / 2;
        if (button(sourceOpen() ? "Restart the camera" : "Start the camera", w2, "F5")) openCamera(s.camera);
        ImGui::SameLine();
        if (button("Stop", w2)) closeSource();
        if (button("Open a video...", w2)) {
            const std::string p = Util::openFile(T("Open a video"), {{"Video", "*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.webm"}, {"Picture", "*.jpg;*.jpeg;*.png;*.bmp"}});
            if (!p.empty()) openFile(p);
        }
        ImGui::SameLine();
        if (button("Track a whole video...", w2, "Every frame of a video tracked into a new take (not in real time)")) {
            const std::string p = Util::openFile(T("Track a video"), {{"Video", "*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.webm"}});
            if (!p.empty()) trackVideo(p);
        }
        check("Mirror the view", &s.mirrorView, "The camera's picture shown as in a mirror (the tracking is the same)");
        if (videoEnded()) { if (button("Replay", -1, "Play the video again from the start")) replayVideo(); }
        check("Loop the video", &s.loopVideo, "A watched video starts over when it reaches its end");
    }
    if (section("Tracking")) {
        sliderChoice("Model", &s.pose.model, {"lite", "full", "heavy"}, DEF.pose.model, "BlazePose's landmark model: lite is fastest, heavy most exact");
        sliderF("Detection", &s.pose.detectScore, 0.1f, 0.95f, "%.2f", DEF.pose.detectScore, "The detector's least score to find a person");
        sliderF("Presence", &s.pose.presence, 0.1f, 0.95f, "%.2f", DEF.pose.presence, "Below this the person is lost and looked for again");
        sliderF("Region", &s.pose.roiScale, 1.0f, 2.0f, "%.2f", DEF.pose.roiScale, "How much room is left round the person (MediaPipe: 1.25)");
        sliderI("Threads", &s.pose.threads, 0, 16, s.pose.threads ? "%d" : T("automatic"), DEF.pose.threads, "ONNX Runtime's threads (0: its choice)");
        check("Refine on the heatmap", &s.pose.refine, "The landmarks sharpened on the model's heatmap, as MediaPipe does");
        if (button("Find the person again", -1, "The detector runs again (R tracks anew)")) resetTracking();
    }
    if (section("Smoothing")) {
        check("Smooth", &s.filter.on, "The One Euro filter on every landmark");
        sliderF("Min cutoff", &s.filter.minCutoff, 0.001f, 2.0f, "%.3f Hz", DEF.filter.minCutoff, "Lower: a still pose is smoother (more lag)", ImGuiSliderFlags_Logarithmic);
        sliderF("Speed", &s.filter.beta, 0.0f, 300.0f, "%.1f", DEF.filter.beta, "Higher: quick moves follow with less lag");
        sliderF("Speed cutoff", &s.filter.dCutoff, 0.1f, 10.0f, "%.2f Hz", DEF.filter.dCutoff, "How the speed itself is smoothed");
        sliderF("World min cutoff", &s.filter.worldMinCutoff, 0.001f, 2.0f, "%.3f Hz", DEF.filter.worldMinCutoff, "The same, for the landmarks in metres", ImGuiSliderFlags_Logarithmic);
        sliderF("World speed", &s.filter.worldBeta, 0.0f, 200.0f, "%.1f", DEF.filter.worldBeta);
        sliderF("World speed cutoff", &s.filter.worldDCutoff, 0.1f, 10.0f, "%.2f Hz", DEF.filter.worldDCutoff);
        sliderF("Sure above", &s.filter.visibility, 0.0f, 1.0f, "%.2f", DEF.filter.visibility, "A landmark less visible than this is unsure");
        sliderF("Hold unsure", &s.filter.holdSeconds, 0.0f, 3.0f, "%.2f s", DEF.filter.holdSeconds, "An unsure landmark stays where it was last seen this long");
    }
    if (section("Solving")) {
        bool skel = false;
        int rest = s.tPose ? 1 : 0;
        if (sliderChoice("Rest pose", &rest, {"A pose", "T pose"}, 0, "The skeleton's rest pose in the exports")) { s.tPose = rest == 1; skel = true; }
        if (sliderF("Height", &s.height, 1.0f, 2.3f, "%.2f m", DEF.height, "The skeleton's height")) skel = true;
        if (skel) skeletonChanged();
        if (sliderChoice("Bone names", &s.exportOpts.naming, {"game engine", "Mixamo", "Mixamo (no prefix)"}, DEF.exportOpts.naming, "The bones' names in the exports and the live link")) skeletonChanged();
        check("Mirror the performer", &s.solver.mirror, "Your left moves the skeleton's right, as a mirror");
        sliderChoice("Root motion", &s.solver.rootMode, {"in place", "across the floor", "across and up (jumps)"}, DEF.solver.rootMode, "Where the root goes as you move");
        check("Feet on the floor", &s.solver.groundLock, "The lowest foot kept on the floor");
        sliderF("Root distance", &s.solver.rootScale, 0.0f, 3.0f, "%.2f", DEF.solver.rootScale, "How far the root goes for a metre you go");
        sliderF("Camera's view", &s.solver.fov, 30.0f, 120.0f, "%.0f\xc2\xb0", DEF.solver.fov, "The camera's field of view across (for your distance)");
        if (button("Recentre", -1, "Where you stand now becomes the middle of the floor (C)")) resetTracking();
        sliderF("Head tilt", &s.solver.headPitch, -30.0f, 45.0f, "%.0f\xc2\xb0", DEF.solver.headPitch, "How far the nose sits under the ears when you look ahead");
        sliderF("Shoulders", &s.solver.shrug, 0.0f, 1.0f, "%.2f", DEF.solver.shrug, "How much the shoulders (clavicles) follow");
        sliderF("Spine", &s.solver.spineBend, 0.0f, 1.0f, "%.2f", DEF.solver.spineBend, "How much the spine shares the chest's turn");
        sliderF("Forearm twist", &s.solver.forearmTwist, 0.0f, 1.0f, "%.2f", DEF.solver.forearmTwist, "The palm turning the forearm");
        sliderF("Limb seen above", &s.solver.visibility, 0.0f, 1.0f, "%.2f", DEF.solver.visibility, "A limb whose landmarks are less sure is unseen");
        sliderChoice("Unseen limbs", &s.solver.unseen, {"at rest", "held"}, DEF.solver.unseen, "Legs out of the picture: as at rest, or held as last seen");
        check("Torso", &s.solver.torso); ImGui::SameLine(); check("Head", &s.solver.head); ImGui::SameLine();
        check("Arms", &s.solver.arms); ImGui::SameLine(); check("Legs", &s.solver.legs);
        sliderF("Planted under", &s.solver.contactHeight, 0.0f, 0.2f, "%.3f m", DEF.solver.contactHeight, "A foot this near the floor...");
        sliderF("Planted slower than", &s.solver.contactSpeed, 0.0f, 2.0f, "%.2f m/s", DEF.solver.contactSpeed, "...and this slow is planted");
        check("Planted feet don't slide", &s.solver.footLock, "The root moves instead");
    }
    if (section("Hands", false)) {
        if (check("Track the hands", &s.hands.on, "MediaPipe Hands for the fingers (slower)") && s.hands.on && !missingModels().empty()) downloadModels();
        sliderF("Hand presence", &s.hands.presence, 0.1f, 0.95f, "%.2f", DEF.hands.presence);
        sliderF("Hand region", &s.hands.roiScale, 1.5f, 4.0f, "%.2f", DEF.hands.roiScale, "The region round each hand (MediaPipe: 2.7)");
        sliderF("Curl", &s.hands.curl, 0.0f, 1.0f, "%.2f", DEF.hands.curl, "How much of the fingers' bend goes on");
        sliderF("Spread", &s.hands.spread, 0.0f, 1.0f, "%.2f", DEF.hands.spread);
        sliderF("Finger smoothing", &s.hands.smooth, 0.0f, 0.95f, "%.2f", DEF.hands.smooth);
    }
    if (section("Face", false)) {
        if (check("Track the face", &s.face.on, "MediaPipe's Face Landmarker: 478 points and ARKit's 52 blendshapes") && s.face.on && !missingModels().empty()) downloadModels();
        check("Start from the body's face", &s.face.fromBody, "When the body is in view, the face is looked for where the body's face is");
        sliderF("Face detection", &s.face.detectScore, 0.1f, 0.95f, "%.2f", DEF.face.detectScore);
        sliderF("Face presence", &s.face.presence, 0.1f, 0.95f, "%.2f", DEF.face.presence);
        sliderF("Face region", &s.face.roiScale, 1.1f, 2.5f, "%.2f", DEF.face.roiScale, "The region round the face (MediaPipe: 1.5)");
        sliderF("Face smoothing", &s.face.smooth, 0.0f, 0.95f, "%.2f", DEF.face.smooth, "The mesh's points from frame to frame");
        sliderF("Blendshape smoothing", &s.face.weightSmooth, 0.0f, 0.95f, "%.2f", DEF.face.weightSmooth);
        sliderF("Expression strength", &s.face.gain, 0.0f, 2.0f, "%.2f", DEF.face.gain, "Every blendshape times this");
        check("Eyes from the irises", &s.face.eyesFromIris, "eyeLook from where each iris sits in its eye, instead of the model's");
    }
    if (section("Sharpie markers", false)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", T("Dots drawn on the face with a Sharpie, tracked to sharpen the face where it moves most."));
        ImGui::PopTextWrapPos();
        if (check("Use the markers", &s.markers.on, "Needs the face tracked") && s.markers.on && !s.face.on) { s.face.on = true; if (!missingModels().empty()) downloadModels(); }
        check("Show where to draw them", &s.markerGuide, "Numbered spots on your face in the camera view");
        const Live LM = live();
        const float bw = (ImGui::GetContentRegionAvail().x - 6) / 2;
        if (button("Calibrate", bw, "Look at the camera with a still, neutral face, then press: each dot is found and kept")) calibrateMarkers();
        ImGui::SameLine();
        if (button("Clear", bw)) clearMarkers();
        if (s.markers.on) {
            if (LM.markers.calibrated) ImGui::Text("%s %d / %d", T("Dots tracked:"), LM.markers.found, Markers::COUNT);
            else ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1), "%s", T("Not calibrated"));
        }
        sliderF("Dot darkness", &s.markers.darkness, 0.05f, 0.6f, "%.2f", DEF.markers.darkness, "How much darker than the skin round it a dot is");
        sliderF("Smallest dot", &s.markers.minDot, 0.002f, 0.03f, "%.3f", DEF.markers.minDot, "Its radius, as a share of the face's width");
        sliderF("Largest dot", &s.markers.maxDot, 0.01f, 0.08f, "%.3f", DEF.markers.maxDot);
        sliderF("Roundness", &s.markers.roundness, 0.2f, 0.9f, "%.2f", DEF.markers.roundness, "Brows and hair are long, dots are round");
        sliderF("Search", &s.markers.search, 0.01f, 0.2f, "%.3f", DEF.markers.search, "How far from where the mesh puts it a dot may be found");
        sliderF("Marker weight", &s.markers.blend, 0.0f, 1.0f, "%.2f", DEF.markers.blend, "How much the dots move the face's mesh (0: only tracked)");
    }
    if (section("Recording")) {
        const bool r = recording();
        const float cd = countdownLeft();
        char lbl[64];
        if (cd > 0) snprintf(lbl, sizeof lbl, "%s %.0f", T("Starting in"), std::ceil(cd));
        else snprintf(lbl, sizeof lbl, "%s", r ? T("\xe2\x96\xa0  Stop") : T("\xe2\x97\x8f  Record"));
        if (r) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (ImGui::Button(lbl, ImVec2(-1, 34))) { if (r) stopRecording(); else startRecording(true); }
        if (r) ImGui::PopStyleColor();
        tip("Record a take from the camera (R)");
        sliderI("Countdown", &s.countdown, 0, 10, "%d s", DEF.countdown, "Seconds to get in place");
    }
    if (section("Clean-up")) {
        bool c = false;
        c |= sliderF("Frame rate", &s.cleanup.fps, 12.0f, 120.0f, "%.0f fps", DEF.cleanup.fps, "The take's frame rate in the exports");
        c |= sliderF("Smoothing", &s.cleanup.smooth, 0.0f, 10.0f, "%.1f frames", DEF.cleanup.smooth, "More smoothing over the whole take (no lag)");
        c |= check("Lock planted feet", &s.cleanup.footLock, "A planted foot held still (the legs bend to keep it)");
        c |= sliderI("Plants from", &s.cleanup.minPlant, 1, 30, "%d frames", DEF.cleanup.minPlant, "Shorter plants are ignored");
        c |= sliderF("Lock easing", &s.cleanup.lockBlend, 0.0f, 15.0f, "%.0f frames", DEF.cleanup.lockBlend);
        c |= check("Loop", &s.cleanup.loop, "The end blended into the start");
        c |= sliderF("Loop blend", &s.cleanup.loopFrames, 1.0f, 60.0f, "%.0f frames", DEF.cleanup.loopFrames);
        c |= check("In place", &s.cleanup.inPlace, "The root stays over the middle");
        c |= check("Start at the middle", &s.cleanup.startAtOrigin, "The take starts at the floor's middle");
        if (c) st.bakedDirty = true;
    }
    if (section("Export")) {
        sliderChoice("Format", &s.exportFormat, {"FBX", "glTF binary (glb)", "BVH"}, DEF.exportFormat);
        sliderF("Scale", &s.exportOpts.scale, 0.01f, 100.0f, "%.2f", DEF.exportOpts.scale, "Metres times this", ImGuiSliderFlags_Logarithmic);
        if (s.exportFormat != 2) check("With a mannequin", &s.exportOpts.mannequin, "A plain body on the bones (glTF needs one for an armature)");
        else sliderChoice("Up", &s.exportOpts.zUp, {"Y", "Z"}, DEF.exportOpts.zUp);
        if (button("Export the take...", -1, "Ctrl+E")) exportDialog(st.sel);
        if (button("Export every take", -1, "Into Documents\\Pantomimus\\Exports")) {
            int n = 0; std::string err;
            for (size_t i = 0; i < st.takes.size(); ++i)
                if (exportTake((int)i, Util::join(exportsDir(), st.takes[i].name + EXT[s.exportFormat]), err)) ++n; else say(err);
            char b[160]; snprintf(b, sizeof b, T("%d takes exported to %s"), n, exportsDir().c_str()); say(b);
        }
    }
    if (section("Live link")) {
        if (check("Stream (Blender, games)", &s.linkOn, "Pantomimus sends the pose to whoever connects")) {
            setLink(s.linkOn);
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##port", &s.linkPort)) { s.linkPort = std::clamp(s.linkPort, 1024, 65535); if (s.linkPort >= 5000 && s.linkPort <= 5999) s.linkPort = 6000; }
        tip("The port (not 5000-5999)");
        if (server.running()) ImGui::TextColored(col4(C_OK), "%s %d, %d %s", T("Listening on"), server.port(), server.clients(), T("connected"));
        if (button("The Blender add-on", -1, "Its folder: install pantomimus_link.py in Blender's Preferences > Add-ons")) Util::openFolder(Util::exeDir() + "blender");
        if (check("Pose Pygmalion", &s.pygOn, "Pygmalion's person moves with you (Pygmalion: Settings > Socket > Accept connections)")) {
            setPygmalion(s.pygOn);
        }
        sliderF("Poses a second", &s.pygRate, 1.0f, 30.0f, "%.0f", DEF.pygRate);
        if (pyg.running()) ImGui::TextWrapped("%s", pyg.status().c_str());
    }
}

// the landmarks drawn over a picture rect (or a well): bones as lines in the sides' colours, joints as dots
static void drawLandmarks(ImDrawList* dl, ImVec2 o, ImVec2 sz, const Pose::Result& r, bool mirror, bool labels, const Hands::Result* hands) {
    auto at = [&](const Pose::Point& p) { const float x = mirror ? 1 - p.x : p.x; return ImVec2(o.x + x * sz.x, o.y + p.y * sz.y); };
    auto sideCol = [](int i) { return i < 11 ? C_MID : (i % 2 == 1 ? C_LEFT : C_RIGHT); };
    for (int k = 0; k < Pose::BONE_COUNT; ++k) {
        const int a = Pose::BONES[k][0], b = Pose::BONES[k][1];
        if (std::min(r.visibility[a], r.visibility[b]) < 0.3f) continue;
        const ImU32 c = (a % 2 == b % 2 && a >= 11) ? sideCol(a) : C_MID;
        dl->AddLine(at(r.image[a]), at(r.image[b]), c, 2.5f);
    }
    for (int i = 0; i < Pose::N; ++i) {
        const bool sure = r.visibility[i] >= 0.5f;
        dl->AddCircleFilled(at(r.image[i]), sure ? 4.0f : 3.0f, sure ? sideCol(i) : C_DIM, 12);
        if (labels) dl->AddText(ImVec2(at(r.image[i]).x + 5, at(r.image[i]).y - 8), C_DIM, T(Pose::landmarkName(i)));
    }
    if (hands)
        for (int side = 0; side < 2; ++side) {
            const Hands::Hand& h = hands->hand[side];
            if (!h.found) continue;
            for (int k = 0; k < Hands::BONE_COUNT; ++k) dl->AddLine(at(h.image[Hands::BONES[k][0]]), at(h.image[Hands::BONES[k][1]]), side == 0 ? C_LEFT : C_RIGHT, 1.5f);
            for (int i = 0; i < Hands::N; ++i) dl->AddCircleFilled(at(h.image[i]), 2.5f, C_MID, 8);
        }
}

static void drawFace(ImDrawList* dl, ImVec2 o, ImVec2 sz, const Face::Result& f, const Markers::Result* mk, bool mirror, bool guide, int fw, int fh) {
    auto at = [&](float x, float y) { if (mirror) x = 1 - x; return ImVec2(o.x + x * sz.x, o.y + y * sz.y); };
    for (const auto& e : Face::contour()) dl->AddLine(at(f.image[e.first].x, f.image[e.first].y), at(f.image[e.second].x, f.image[e.second].y), C_MID, 1.2f);
    for (int iris : {Face::R_IRIS, Face::L_IRIS}) {
        const float r = std::hypot((f.image[iris + 1].x - f.image[iris + 3].x) * sz.x, (f.image[iris + 1].y - f.image[iris + 3].y) * sz.y) / 2;
        dl->AddCircle(at(f.image[iris].x, f.image[iris].y), std::max(2.0f, r), C_BLUE, 16, 1.5f);
    }
    if (guide && (!mk || !mk->calibrated)) {
        // where to draw each dot: numbered, on the performer's own face
        for (int i = 0; i < Markers::COUNT; ++i) {
            float px, py; Markers::spotPosition(f, i, fw, fh, px, py);
            const ImVec2 c = at(px / fw, py / fh);
            dl->AddCircle(c, 5, IM_COL32(80, 220, 120, 255), 12, 2);
            char n[8]; snprintf(n, sizeof n, "%d", i + 1);
            dl->AddText(ImVec2(c.x + 6, c.y - 7), IM_COL32(80, 220, 120, 255), n);
        }
    }
    if (mk) {
        for (const auto& b : mk->blobs) dl->AddCircle(at(b.x / fw, b.y / fh), std::max(2.0f, b.r * sz.x / fw), C_DIM, 10, 1);
        for (int i = 0; i < Markers::COUNT; ++i)
            if (mk->seen[i]) dl->AddCircleFilled(at(mk->x[i], mk->y[i]), 3.5f, IM_COL32(240, 80, 70, 255), 10);
    }
}

static GLuint camTex = 0;
static int camTexW = 0, camTexH = 0;
static long long camTexNo = -1;

static void cameraPanel() {
    const Live L = live();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + avail.x, o.y + avail.y), C_WELL);
    const bool takeView = st.showTake && st.sel >= 0 && st.sel < (int)st.takes.size();
    int fw = L.frame.w, fh = L.frame.h;
    const Pose::Result* lm = L.filtered.found ? &L.filtered : nullptr;
    const Hands::Result* hd = &L.hands;
    Pose::Result takeLm;
    if (takeView) {
        // a take: its landmarks at the playhead, on the well
        const Clip::Take& t = st.takes[st.sel];
        const size_t i = Clip::frameAt(t, std::max(0.0, t.in) + st.playhead);
        if (i < t.raw.size()) { takeLm = t.raw[i]; fw = takeLm.frameW; fh = takeLm.frameH; lm = takeLm.found ? &takeLm : nullptr; }
        hd = i < t.hands.size() ? &t.hands[i] : nullptr;
    } else if (!L.frame.empty() && L.frameNo != camTexNo) {
        if (!camTex) glGenTextures(1, &camTex);
        glBindTexture(GL_TEXTURE_2D, camTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (camTexW != L.frame.w || camTexH != L.frame.h) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, L.frame.w, L.frame.h, 0, GL_RGB, GL_UNSIGNED_BYTE, L.frame.rgb.data());
            camTexW = L.frame.w; camTexH = L.frame.h;
        } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, L.frame.w, L.frame.h, GL_RGB, GL_UNSIGNED_BYTE, L.frame.rgb.data());
        camTexNo = L.frameNo;
    }
    if (fw > 0 && fh > 0) {
        const float k = std::min(avail.x / fw, avail.y / fh);
        const ImVec2 sz(fw * k, fh * k), p0(o.x + (avail.x - sz.x) / 2, o.y + (avail.y - sz.y) / 2);
        const bool mirror = st.set.mirrorView;
        if (!takeView && camTex) dl->AddImage((ImTextureID)(intptr_t)camTex, p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImVec2(mirror ? 1.0f : 0.0f, 0), ImVec2(mirror ? 0.0f : 1.0f, 1));
        else dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), C_LINE);
        if (st.set.overlay && lm) drawLandmarks(dl, p0, sz, *lm, mirror, st.set.overlayLabels, hd);
        if (st.set.overlay && !takeView && L.face.found) drawFace(dl, p0, sz, L.face, st.set.markers.on ? &L.markers : nullptr, mirror, st.set.markers.on && st.set.markerGuide, fw, fh);
        if (st.set.overlayRoi && !takeView && L.raw.roi.valid) {
            const Pose::Roi& r = L.raw.roi;
            const float c = std::cos(r.rot), s = std::sin(r.rot), h = r.size / 2;
            ImVec2 q[4];
            const float cs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i = 0; i < 4; ++i) {
                float x = (r.cx + cs[i][0] * h * c - cs[i][1] * h * s) / fw, y = (r.cy + cs[i][0] * h * s + cs[i][1] * h * c) / fh;
                if (mirror) x = 1 - x;
                q[i] = ImVec2(p0.x + x * sz.x, p0.y + y * sz.y);
            }
            dl->AddPolyline(q, 4, C_DIM, ImDrawFlags_Closed, 1);
        }
    }
    // what's going on, top left
    const char* msg = nullptr;
    if (takeView) msg = st.takes[st.sel].name.c_str();
    else if (L.frame.empty()) msg = !L.error.empty() ? L.error.c_str() : T("Start the camera (F5), or open a video");
    if (msg) dl->AddText(ImVec2(o.x + 12, o.y + 10), takeView ? C_DIM : (L.error.empty() ? C_DIM : C_ERROR), msg);
    if (!takeView && !L.frame.empty() && !L.filtered.found && !L.face.found) dl->AddText(ImVec2(o.x + 12, o.y + 10), C_WARN, T("No one in view"));
    const float cd = countdownLeft();
    if (cd > 0) {
        char b[8]; snprintf(b, sizeof b, "%.0f", std::ceil(cd));
        ImFont* f = ImGui::GetFont();
        dl->AddText(f, 120, ImVec2(o.x + avail.x / 2 - 30, o.y + avail.y / 2 - 70), C_TEXT, b);
    } else if (recording() && !takeView) {
        dl->AddCircleFilled(ImVec2(o.x + avail.x - 24, o.y + 20), 7, C_ERROR, 16);
        dl->AddText(ImVec2(o.x + avail.x - 70, o.y + 12), C_TEXT, "REC");
    }
    ImGui::InvisibleButton("##cam", ImVec2(std::max(1.0f, avail.x), std::max(1.0f, avail.y)));
    if (ImGui::BeginPopupContextItem("##camctx")) {
        if (ImGui::MenuItem(T("Mirror the view"), nullptr, &st.set.mirrorView)) changed = true;
        if (ImGui::MenuItem(T("Show the landmarks"), nullptr, &st.set.overlay)) changed = true;
        if (ImGui::MenuItem(T("Show their names"), nullptr, &st.set.overlayLabels)) changed = true;
        if (ImGui::MenuItem(T("Show the region"), nullptr, &st.set.overlayRoi)) changed = true;
        ImGui::EndPopup();
    }
}

static View3D::Camera cam3;
static void viewPanel() {
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const int w = std::max(8, (int)avail.x), h = std::max(8, (int)avail.y);
    const Live L = live();
    const Skel::Pose* pose = nullptr;
    Skel::Pose takePose;
    bool contact[2] = {false, false};
    const Pose::Result* lm = nullptr;
    const Face::Result* face = nullptr;
    Face::Result takeFace;
    float aspect = 1.0f;
    if (st.showTake && !st.baked.empty()) {
        takePose = Clip::sample(st.baked, st.playhead, st.skel);
        pose = &takePose;
        const size_t f = std::min(st.baked.frames.size() - 1, (size_t)(st.playhead * st.baked.fps));
        contact[0] = st.baked.frames[f].contact[0]; contact[1] = st.baked.frames[f].contact[1];
        // the take's face at the playhead, if it has one
        if (st.sel >= 0 && st.sel < (int)st.takes.size()) {
            const Clip::Take& t = st.takes[st.sel];
            if (!t.faces.empty()) {
                const size_t i = Clip::frameAt(t, std::max(0.0, t.in) + st.playhead);
                if (i < t.faces.size() && t.faces[i].found) { takeFace = t.faces[i]; face = &takeFace; }
                if (i < t.raw.size() && t.raw[i].frameH > 0) aspect = (float)t.raw[i].frameW / t.raw[i].frameH;
            }
        }
    } else if (L.filtered.found || L.face.found) {
        if (L.filtered.found) { pose = &L.solved.pose; contact[0] = L.solved.contact[0]; contact[1] = L.solved.contact[1]; lm = &L.filtered; }
        if (st.set.face.on && L.face.found) face = &L.face;
        if (!L.frame.empty()) aspect = (float)L.frame.w / std::max(1, L.frame.h);
    }
    const GLuint tex = View3D::render(w, h, cam3, st.set.look, st.skel, pose, contact, lm, st.set.solver.mirror, face, aspect);
    if (tex) ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, o, ImVec2(o.x + w, o.y + h), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::InvisibleButton("##view", ImVec2((float)w, (float)h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) View3D::orbit(cam3, io.MouseDelta.x, io.MouseDelta.y);
        else View3D::pan(cam3, io.MouseDelta.x, io.MouseDelta.y, h);
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0) View3D::zoom(cam3, io.MouseWheel);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) cam3.reset();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(o.x + 10, o.y + 8), C_DIM, st.showTake && !st.baked.empty() ? T("Take") : T("Live"));
    ImGui::SetCursorScreenPos(ImVec2(o.x + w - 330, o.y + 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    if (ImGui::Checkbox(T("Body"), &st.set.look.mannequin)) changed = true; ImGui::SameLine();
    if (st.set.face.on) { if (ImGui::Checkbox(T("Face"), &st.set.look.face)) changed = true; ImGui::SameLine(); }
    if (ImGui::Checkbox(T("Floor"), &st.set.look.floor)) changed = true; ImGui::SameLine();
    if (ImGui::Checkbox(T("Points"), &st.set.look.landmarks)) changed = true;
    ImGui::PopStyleVar();
}

static void timelinePanel() {
    const bool has = st.sel >= 0 && st.sel < (int)st.takes.size();
    ImGui::BeginDisabled(!has);
    const float bw = 34;
    if (ImGui::Button("\xe2\x8f\xae", ImVec2(bw, 0))) st.playhead = 0;
    tip("To the start (Home)");
    ImGui::SameLine();
    if (ImGui::Button(st.playing ? "\xe2\x8f\xb8" : "\xe2\x96\xb6", ImVec2(bw, 0))) { st.playing = !st.playing; st.showTake = true; }
    tip("Play / pause (Space)");
    ImGui::SameLine();
    if (ImGui::Button("\xe2\x8f\xad", ImVec2(bw, 0))) st.playhead = st.baked.duration();
    tip("To the end (End)");
    ImGui::SameLine();
    ImGui::PushFont(fontMono);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", clock(st.playhead).c_str(), clock(st.baked.duration()).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (button("Set in", 0, "The take starts here (I)") && has) {
        pushUndo("Trim"); Clip::Take& t = st.takes[st.sel]; t.in = std::max(0.0, t.in) + st.playhead; st.playhead = 0; st.bakedDirty = true;
    }
    ImGui::SameLine();
    if (button("Set out", 0, "The take ends here (O)") && has) {
        pushUndo("Trim"); Clip::Take& t = st.takes[st.sel]; t.out = std::max(0.0, t.in) + st.playhead; st.bakedDirty = true;
    }
    ImGui::SameLine();
    if (button("Untrim", 0) && has) { pushUndo("Trim"); st.takes[st.sel].in = 0; st.takes[st.sel].out = -1; st.bakedDirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox(T("Show the take"), &st.showTake)) {}
    ImGui::EndDisabled();
    // the track: the whole take, its trimmed part, the planted feet, the playhead
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x, H = std::max(48.0f, ImGui::GetContentRegionAvail().y - 4);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), C_WELL);
    if (has) {
        const Clip::Take& t = st.takes[st.sel];
        const double len = std::max(1e-3, t.length());
        auto X = [&](double s) { return o.x + (float)(s / len) * W; };
        const double in = std::max(0.0, t.in), outp = t.outPoint();
        dl->AddRectFilled(ImVec2(X(in), o.y), ImVec2(X(outp), o.y + H), C_RAISED);
        // the frames where nobody was found
        for (size_t i = 0; i + 1 < t.raw.size(); ++i) if (!t.raw[i].found) dl->AddRectFilled(ImVec2(X(t.t[i]), o.y + H - 6), ImVec2(X(t.t[i + 1]) + 1, o.y + H), C_ERROR);
        // planted feet, from the baked take
        if (!st.baked.empty())
            for (size_t f = 0; f < st.baked.frames.size(); ++f) {
                const double s0 = in + f / (double)st.baked.fps, s1 = s0 + 1 / (double)st.baked.fps;
                for (int side = 0; side < 2; ++side)
                    if (st.baked.frames[f].contact[side]) dl->AddRectFilled(ImVec2(X(s0), o.y + 8 + side * 12), ImVec2(X(s1) + 1, o.y + 16 + side * 12), side == 0 ? C_LEFT : C_RIGHT);
            }
        dl->AddText(ImVec2(o.x + 4, o.y + 34), C_DIM, T("feet planted: left, right"));
        // ticks each second
        for (int s = 0; s <= (int)len; ++s) dl->AddLine(ImVec2(X(s), o.y + H - 10), ImVec2(X(s), o.y + H), C_LINE);
        dl->AddLine(ImVec2(X(in), o.y), ImVec2(X(in), o.y + H), C_DEEP, 2);
        dl->AddLine(ImVec2(X(outp), o.y), ImVec2(X(outp), o.y + H), C_DEEP, 2);
        const float px = X(in + st.playhead);
        dl->AddLine(ImVec2(px, o.y), ImVec2(px, o.y + H), C_BLUE, 2);
        dl->AddTriangleFilled(ImVec2(px - 6, o.y), ImVec2(px + 6, o.y), ImVec2(px, o.y + 8), C_BLUE);
        ImGui::InvisibleButton("##track", ImVec2(W, H));
        if (ImGui::IsItemActive()) {
            const double s = std::clamp((ImGui::GetIO().MousePos.x - o.x) / W * len, in, outp);
            st.playhead = s - in; st.showTake = true; st.playing = false;
        }
    } else {
        dl->AddText(ImVec2(o.x + 10, o.y + 10), C_DIM, T("Record a take (R), or track a whole video, and it shows here"));
        ImGui::Dummy(ImVec2(W, H));
    }
}

static char renameBuf[128];
static int renaming = -1;
static void takesPanel() {
    if (button("New project", (ImGui::GetContentRegionAvail().x - 6) / 2)) newProject();
    ImGui::SameLine();
    if (button("Export...", -1)) exportDialog(st.sel);
    if (st.takes.empty()) { ImGui::TextDisabled("%s", T("No takes yet")); return; }
    if (ImGui::BeginTable("##takes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(T("Take"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(T("Length"), ImGuiTableColumnFlags_WidthFixed, 70);
        for (int i = 0; i < (int)st.takes.size(); ++i) {
            Clip::Take& t = st.takes[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (renaming == i) {
                ImGui::SetNextItemWidth(-1);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rn", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    if (renameBuf[0] && t.name != renameBuf) { pushUndo("Rename take"); st.takes[i].name = renameBuf; }
                    renaming = -1;
                }
                if (ImGui::IsItemDeactivated()) renaming = -1;
            } else {
                if (ImGui::Selectable(t.name.c_str(), st.sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    select(i);
                    if (ImGui::IsMouseDoubleClicked(0)) { renaming = i; snprintf(renameBuf, sizeof renameBuf, "%s", t.name.c_str()); }
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(T("Rename"))) { renaming = i; snprintf(renameBuf, sizeof renameBuf, "%s", t.name.c_str()); }
                    if (ImGui::MenuItem(T("Duplicate"), "Ctrl+D")) duplicateTake(i);
                    if (ImGui::MenuItem(T("Export..."))) exportDialog(i);
                    if (ImGui::MenuItem(T("Delete"), "Del")) deleteTake(i);
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s\n%zu %s", t.source.c_str(), t.frames(), T("frames"));
            }
            ImGui::TableNextColumn();
            ImGui::PushFont(fontMono);
            ImGui::TextDisabled("%s", clock(t.outPoint() - std::max(0.0, t.in)).c_str());
            ImGui::PopFont();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void bar(float v, bool strong) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + 4), ImVec2(p.x + 84, p.y + 14), C_WELL);
    dl->AddRectFilled(ImVec2(p.x, p.y + 4), ImVec2(p.x + 84 * std::clamp(v, 0.0f, 1.0f), p.y + 14), strong ? C_BLUE : C_DIM);
    ImGui::Dummy(ImVec2(84, 16));
}

static void facePanel(const Live& L) {
    const Face::Result& f = L.face;
    if (!st.set.face.on) { ImGui::TextDisabled("%s", T("Turn on Face in the controls")); return; }
    ImGui::TextDisabled("%s %.2f   %s %.0f  %s %.0f  %s %.0f", T("Face:"), f.found ? f.score : 0.0f, T("yaw"), f.yaw, T("pitch"), f.pitch, T("roll"), f.roll);
    if (ImGui::BeginTable("##bs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(T("Blendshape"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(T("Weight"), ImGuiTableColumnFlags_WidthFixed, 90);
        for (int i = 1; i < Face::BLENDSHAPES; ++i) {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextUnformatted(Face::BLENDSHAPE_NAMES[i]);
            ImGui::TableNextColumn();
            bar(f.found ? f.weights[i] : 0, true);
        }
        ImGui::EndTable();
    }
}

static void markersPanel(const Live& L) {
    if (!st.set.markers.on) { ImGui::TextDisabled("%s", T("Turn on Sharpie markers in the controls")); return; }
    ImGui::TextDisabled("%s %d / %d", T("Dots:"), L.markers.found, Markers::COUNT);
    if (ImGui::BeginTable("##mk", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(T("Marker"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(T("Seen"), ImGuiTableColumnFlags_WidthFixed, 90);
        for (int i = 0; i < Markers::COUNT; ++i) {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("%d  %s", i + 1, T(Markers::LAYOUT[i].name));
            ImGui::TableNextColumn();
            bar(L.markers.seen[i] ? 1.0f : 0.0f, true);
        }
        ImGui::EndTable();
    }
}

static void landmarksPanel() {
    const Live L = live();
    const Pose::Result& r = L.filtered;
    if (!ImGui::BeginTabBar("##lmtabs")) return;
    if (ImGui::BeginTabItem(T("Body"))) {
        ImGui::TextDisabled("%s %.2f", T("Person:"), r.found ? r.score : 0.0f);
        if (ImGui::BeginTable("##lm", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn(T("Landmark"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(T("Seen"), ImGuiTableColumnFlags_WidthFixed, 90);
            for (int i = 0; i < Pose::N; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(T(Pose::landmarkName(i)));
                ImGui::TableNextColumn();
                const float v = r.found ? r.visibility[i] : 0;
                bar(v, v >= st.set.filter.visibility);
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("Face"))) { facePanel(L); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem(T("Markers"))) { markersPanel(L); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
}

static void prefsWindow() {
    if (!st.showPrefs) return;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(T("Preferences"), &st.showPrefs, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushFont(fontBold); ImGui::TextUnformatted(T("Models")); ImGui::PopFont();
        ImGui::TextWrapped("%s", st.modelsDir().c_str());
        if (button("Choose a folder...")) { const std::string p = Util::pickFolder(T("The models folder")); if (!p.empty()) { st.set.modelsDir = p; changed = true; } }
        ImGui::SameLine();
        if (button("Open it")) { Util::makeDirs(st.modelsDir()); Util::openFolder(st.modelsDir()); }
        ImGui::SameLine();
        if (button("The default")) { st.set.modelsDir.clear(); changed = true; }
        const auto miss = missingModels();
        if (miss.empty()) ImGui::TextColored(col4(C_OK), "%s", T("The models are all there"));
        else {
            ImGui::TextColored(col4(C_WARN), "%s %zu", T("Models missing:"), miss.size());
            if (downloadProgress() < 0 && button("Download them")) downloadModels();
        }
        ImGui::Separator();
        ImGui::PushFont(fontBold); ImGui::TextUnformatted(T("Starting")); ImGui::PopFont();
        check("Open the last project at start", &st.set.openLast);
        check("Start the camera at start", &st.set.cameraAtStart);
        ImGui::Separator();
        ImGui::PushFont(fontBold); ImGui::TextUnformatted(T("Language")); ImGui::PopFont();
        ImGui::SetNextItemWidth(260);
        std::string curName = "English";
        for (const auto& p : Lang::packs()) if (p.code == Lang::current()) curName = p.name;
        if (ImGui::BeginCombo("##lang", curName.c_str())) {
            for (const auto& p : Lang::packs()) if (ImGui::Selectable(p.name.c_str(), p.code == Lang::current())) { setLanguage(p.code); say(T("The new language shows fully after a restart (its letters)")); changed = true; }
            ImGui::EndCombo();
        }
        ImGui::Separator();
        if (button("Every setting back to its default")) {
            const std::string md = st.set.modelsDir, lang = st.set.language, last = st.set.lastProject;
            st.set = Settings(); st.set.modelsDir = md; st.set.language = lang; st.set.lastProject = last;
            changed = true; skeletonChanged();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- the window
static bool resetLayout = false;
static bool panels[6] = {true, true, true, true, true, true};
static const char* PANEL_NAMES[6] = {"Controls", "Camera", "3D view", "Timeline", "Takes", "Landmarks"};
static std::string panelTitle(int i) { return std::string(T(PANEL_NAMES[i])) + "###" + PANEL_NAMES[i]; }

static void openProjectDialog() {
    const std::string p = Util::openFile(T("Open a project"), {{"Pantomimus project", "*.pantomimus"}});
    if (p.empty()) return;
    std::string err;
    if (openProject(p, err)) say(std::string(T("Opened: ")) + p); else say(err);
}
static void saveProjectDialog(bool as) {
    std::string p = st.project;
    if (as || p.empty()) p = Util::saveFile(T("Save the project"), Util::join(Util::documentsDir(), "Motion.pantomimus"), {{"Pantomimus project", "*.pantomimus"}});
    if (p.empty()) return;
    std::string err;
    if (saveProject(p, err)) say(std::string(T("Saved: ")) + p); else say(err);
}

static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(T("File"))) {
        if (ImGui::MenuItem(T("New project"), "Ctrl+N")) newProject();
        if (ImGui::MenuItem(T("Open project..."), "Ctrl+O")) openProjectDialog();
        if (ImGui::MenuItem(T("Save project"), "Ctrl+S")) saveProjectDialog(false);
        if (ImGui::MenuItem(T("Save project as..."), "Ctrl+Shift+S")) saveProjectDialog(true);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Open a video..."))) { const std::string p = Util::openFile(T("Open a video"), {{"Video", "*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.webm"}, {"Picture", "*.jpg;*.jpeg;*.png;*.bmp"}}); if (!p.empty()) openFile(p); }
        if (ImGui::MenuItem(T("Track a whole video..."))) { const std::string p = Util::openFile(T("Track a video"), {{"Video", "*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.webm"}}); if (!p.empty()) trackVideo(p); }
        ImGui::Separator();
        if (ImGui::MenuItem(T("Export the take..."), "Ctrl+E", false, st.sel >= 0)) exportDialog(st.sel);
        const bool hasFace = st.sel >= 0 && st.sel < (int)st.takes.size() && !st.takes[st.sel].faces.empty();
        const bool hasMarks = st.sel >= 0 && st.sel < (int)st.takes.size() && !st.takes[st.sel].markers.empty();
        if (ImGui::MenuItem(T("Export the face (Live Link Face CSV)..."), nullptr, false, hasFace)) {
            const std::string path = Util::saveFile(T("Export the face"), st.takes[st.sel].name + " face.csv", {{"CSV", "*.csv"}});
            std::string err;
            if (!path.empty()) say(Clip::exportFaceCsv(st.takes[st.sel], st.set.cleanup.fps, path, err) ? std::string(T("Exported: ")) + path : err);
        }
        if (ImGui::MenuItem(T("Export the markers (CSV)..."), nullptr, false, hasMarks)) {
            const std::string path = Util::saveFile(T("Export the markers"), st.takes[st.sel].name + " markers.csv", {{"CSV", "*.csv"}});
            std::string err;
            if (!path.empty()) say(Clip::exportMarkersCsv(st.takes[st.sel], path, err) ? std::string(T("Exported: ")) + path : err);
        }
        if (ImGui::MenuItem(T("Open the exports folder"))) Util::openFolder(exportsDir());
        ImGui::Separator();
        if (ImGui::MenuItem(T("Quit"), "Alt+F4")) st.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Edit"))) {
        if (ImGui::MenuItem(T("Undo"), "Ctrl+Z", false, !undoStack.empty())) undo();
        if (ImGui::MenuItem(T("Redo"), "Ctrl+Y", false, !redoStack.empty())) redo();
        ImGui::Separator();
        if (ImGui::MenuItem(T("Duplicate the take"), "Ctrl+D", false, st.sel >= 0)) duplicateTake(st.sel);
        if (ImGui::MenuItem(T("Delete the take"), "Del", false, st.sel >= 0)) deleteTake(st.sel);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Preferences..."), "Ctrl+,")) st.showPrefs = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Capture"))) {
        if (ImGui::MenuItem(T("Start the camera"), "F5")) openCamera(st.set.camera);
        if (ImGui::MenuItem(T("Stop the source"))) closeSource();
        ImGui::Separator();
        if (ImGui::MenuItem(recording() ? T("Stop recTake") : T("Record"), "R")) { if (recording()) stopRecording(); else startRecording(true); }
        if (ImGui::MenuItem(T("Recentre"), "C")) resetTracking();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("View"))) {
        if (ImGui::MenuItem(T("Mirror the view"), nullptr, &st.set.mirrorView)) changed = true;
        if (ImGui::MenuItem(T("Show the landmarks"), nullptr, &st.set.overlay)) changed = true;
        if (ImGui::MenuItem(T("Show their names"), nullptr, &st.set.overlayLabels)) changed = true;
        if (ImGui::MenuItem(T("Show the region"), nullptr, &st.set.overlayRoi)) changed = true;
        ImGui::Separator();
        if (ImGui::MenuItem(T("The body in 3D"), nullptr, &st.set.look.mannequin)) changed = true;
        if (ImGui::MenuItem(T("The bones in 3D"), nullptr, &st.set.look.bones)) changed = true;
        if (ImGui::MenuItem(T("The floor"), nullptr, &st.set.look.floor)) changed = true;
        if (ImGui::MenuItem(T("The landmarks in 3D"), nullptr, &st.set.look.landmarks)) changed = true;
        if (ImGui::MenuItem(T("Follow the body"), nullptr, &st.set.look.follow)) changed = true;
        if (ImGui::MenuItem(T("Reset the 3D view"))) cam3.reset();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Window"))) {
        for (int i = 0; i < 6; ++i) ImGui::MenuItem(T(PANEL_NAMES[i]), nullptr, &panels[i]);
        ImGui::Separator();
        if (ImGui::MenuItem(T("Reset layout"))) { resetLayout = true; for (bool& p : panels) p = true; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Language"))) {
        for (const auto& p : Lang::packs())
            if (ImGui::MenuItem(p.name.c_str(), nullptr, Lang::current() == p.code)) { setLanguage(p.code); changed = true; say(T("The new language shows fully after a restart (its letters)")); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Help"))) {
        if (ImGui::MenuItem(T("About Pantomimus"))) st.showAbout = true;
        if (ImGui::MenuItem(T("The Blender add-on"))) Util::openFolder(Util::exeDir() + "blender");
        ImGui::Separator();
        if (ImGui::MenuItem("GitHub")) Util::openUrl("https://github.com/CursedPrograms/Pantomimus");
        if (ImGui::MenuItem("GitLab")) Util::openUrl("https://gitlab.com/Cursed.Entertainment");
        if (ImGui::MenuItem("Ko-fi")) Util::openUrl("https://ko-fi.com/cursedentertainment");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

static void statusBar(const ImGuiViewport* vp, float h) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    const Live L = live();
    std::string left = st.project.empty() ? std::string(T("Untitled")) : Util::fileName(st.project);
    if (st.modified) left += " *";
    ImGui::TextUnformatted(left.c_str());
    ImGui::SameLine(0, 24);
    if (!L.source.empty()) { ImGui::TextDisabled("%s", L.source.c_str()); ImGui::SameLine(0, 24); }
    if (!L.frame.empty()) {
        ImGui::TextDisabled("%.0f fps, %.0f ms", L.fps, L.ms);
        ImGui::SameLine(0, 24);
        if (L.filtered.found) ImGui::TextColored(col4(C_OK), "%s", T("tracking")); else ImGui::TextColored(col4(C_WARN), "%s", T("no one in view"));
        ImGui::SameLine(0, 24);
    }
    if (recording()) { ImGui::TextColored(col4(C_ERROR), "%s", T("recTake")); ImGui::SameLine(0, 24); }
    const float dp = downloadProgress();
    if (dp >= 0) { ImGui::TextColored(col4(C_BLUE), "%s %.0f%%", T("Downloading the models"), dp * 100); ImGui::SameLine(0, 24); }
    if (!st.status.empty() && Util::now() - st.statusTime < 12) ImGui::TextUnformatted(st.status.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void dockspace(const ImGuiViewport* vp, float statusH) {
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - statusH));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    const ImGuiID dock = ImGui::GetID("PantomimusDock");
    if (resetLayout || !ImGui::DockBuilderGetNode(dock)) {
        resetLayout = false;
        const float W = vp->WorkSize.x, H = vp->WorkSize.y - statusH;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImVec2(W, H));
        ImGuiID mid = dock;
        const ImGuiID left = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Left, 330.0f / W, nullptr, &mid);
        const ImGuiID right = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Right, 300.0f / (W - 330), nullptr, &mid);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, std::min(0.3f, 170.0f / H), nullptr, &mid);
        ImGuiID camNode = mid;
        const ImGuiID viewNode = ImGui::DockBuilderSplitNode(camNode, ImGuiDir_Right, 0.45f, nullptr, &camNode);
        ImGui::DockBuilderDockWindow("###Controls", left);
        ImGui::DockBuilderDockWindow("###Camera", camNode);
        ImGui::DockBuilderDockWindow("###3D view", viewNode);
        ImGui::DockBuilderDockWindow("###Timeline", bottom);
        ImGui::DockBuilderDockWindow("###Takes", right);
        ImGui::DockBuilderDockWindow("###Landmarks", right);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
}

static void drawPanels() {
    void (*fns[6])() = {controlsPanel, cameraPanel, viewPanel, timelinePanel, takesPanel, landmarksPanel};
    for (int i = 0; i < 6; ++i) {
        if (!panels[i]) continue;
        const bool noPad = i == 1 || i == 2;
        if (noPad) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = ImGui::Begin(panelTitle(i).c_str(), &panels[i], noPad ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        if (noPad) ImGui::PopStyleVar();
        if (open) fns[i]();
        ImGui::End();
    }
}

// keyboard shortcuts first, whatever has focus (not while typing)
static void shortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (ctrl && key(ImGuiKey_Z)) undo();
    else if (ctrl && key(ImGuiKey_Y)) redo();
    else if (ctrl && key(ImGuiKey_S)) saveProjectDialog(shift);
    else if (ctrl && key(ImGuiKey_O)) openProjectDialog();
    else if (ctrl && key(ImGuiKey_N)) newProject();
    else if (ctrl && key(ImGuiKey_E)) exportDialog(st.sel);
    else if (ctrl && key(ImGuiKey_D)) duplicateTake(st.sel);
    else if (ctrl && key(ImGuiKey_Comma)) st.showPrefs = true;
    else if (ctrl) return;
    else if (key(ImGuiKey_R)) { if (recording()) stopRecording(); else startRecording(true); }
    else if (key(ImGuiKey_Space)) { if (st.sel >= 0) { st.playing = !st.playing; st.showTake = true; } }
    else if (key(ImGuiKey_Home)) st.playhead = 0;
    else if (key(ImGuiKey_End)) st.playhead = st.baked.duration();
    else if (key(ImGuiKey_Delete)) deleteTake(st.sel);
    else if (key(ImGuiKey_F5)) openCamera(st.set.camera);
    else if (key(ImGuiKey_C)) resetTracking();
    else if (key(ImGuiKey_L)) st.showTake = false;
    else if (key(ImGuiKey_LeftArrow)) st.playhead = std::max(0.0, st.playhead - 1.0 / std::max(1.0f, st.baked.fps));
    else if (key(ImGuiKey_RightArrow)) st.playhead = std::min(st.baked.duration(), st.playhead + 1.0 / std::max(1.0f, st.baked.fps));
}

// ---------------------------------------------------------------- main
static void attachConsole() {
#ifdef _WIN32
    // tests print lines: to the pipe, file or console the program was started from
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) App::outHandle = h;
    else if (AttachConsole(ATTACH_PARENT_PROCESS))
        App::outHandle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
#endif
}

int run(int argc, char** argv) {
    std::string capture, script, source, lang;
    int camArg = -1;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--source" && i + 1 < argc) source = argv[++i];
        else if (a == "--camera" && i + 1 < argc) camArg = atoi(argv[++i]);
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else if (a == "--selftest") selftest = true;
    }
    const bool test = selftest || !script.empty() || !capture.empty();
    if (test) attachConsole();
    if (!test) loadSettings();
    Lang::load(Util::exeDir() + "lang", lang.empty() ? st.set.language : lang);
    st.set.language = Lang::current();
    {
        std::string err;
        if (!Skel::load(st.measured, Util::exeDir() + "data/skeletons/game_engine.skel", err)) out("Pantomimus: " + err);
        skeletonChanged();
    }
    if (selftest) return SelfTest::run(Util::exeDir() + "tests");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    const int winW = test ? 1600 : 1440, winH = test ? 900 : 900;
    SDL_Window* window = SDL_CreateWindow("Pantomimus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 1024, 640);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(test ? 0 : 1);
    SDL_Surface* icon = SDL_LoadBMP((Util::exeDir() + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(window, icon);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string ini;
    ini = test ? std::string() : layoutFile();
    io.IniFilename = ini.empty() ? nullptr : ini.c_str();
    loadFonts(); applyStyle();
    SDL_Window* splashWin = nullptr;
    if (!test) {
        splashWin = SDL_CreateWindow("Pantomimus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(window, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    // loading while the splash shows: ONNX Runtime, the cameras
    std::atomic<bool> loaded{false};
    std::string ortErr;
    std::thread loader([&] {
        Net::runtime(ortErr);
        if (!test) { cams = Src::cameras(); camsListed = true; }
        loaded = true;
    });
    if (splashWin) {
        const Uint32 start = SDL_GetTicks();
        bool skip = false;
        while (true) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) skip = true;
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            const char* what = loaded ? T("Ready") : T("Loading ONNX Runtime and looking for cameras...");
            Splash::draw(ImGui::GetForegroundDrawList(), 0, 0, what, std::min(1.0f, (SDL_GetTicks() - start) / 1500.0f));
            ImGui::Render(); glViewport(0, 0, (int)Splash::W, (int)Splash::H); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); SDL_GL_SwapWindow(splashWin);
            if ((SDL_GetTicks() - start > 1500 || skip) && loaded) break;
            SDL_Delay(16);
        }
    }
    loader.join();
    if (splashWin) {
        ImGui_ImplSDL2_Shutdown(); SDL_GL_MakeCurrent(window, gl); SDL_DestroyWindow(splashWin);
        ImGui_ImplSDL2_InitForOpenGL(window, gl);
    }
    View3D::init();
    if (!ortErr.empty()) say(ortErr);
    settingsChanged();
    worker = std::thread(work);
    if (!test) {
        SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window);
        if (st.set.openLast && !st.set.lastProject.empty()) { std::string err; if (!openProject(st.set.lastProject, err)) say(err); }
        if (st.set.linkOn) { std::string err; if (server.start(st.set.linkPort, err)) server.setSkeleton(st.skel, st.set.exportOpts.naming); else { st.set.linkOn = false; say(err); } }
        if (st.set.pygOn) pyg.start(st.set.pygHost, st.set.pygPort, st.set.pygRate);
    }
    if (!source.empty()) openFile(source);
    else if (camArg >= 0) openCamera(camArg);
    else if (!test && st.set.cameraAtStart) openCamera(st.set.camera);
    bool scripted = false;
    if (!script.empty()) {
        if (!Script::start(script, source)) { out(std::string("Pantomimus: no script called ") + script + " (" + Script::names() + ")"); st.quit = true; }
        scripted = true;
    } else if (!capture.empty()) Script::start("window", source), scripted = true;
    bool askModels = !test && !missingModels().empty();
    int frames = 0;
    double last = Util::now();
    while (!st.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) st.quit = true;
            if (e.type == SDL_DROPFILE && e.drop.file) {
                const std::string p = e.drop.file;
                SDL_free(e.drop.file);
                if (Util::extension(p) == ".pantomimus") { std::string err; if (!openProject(p, err)) say(err); } else openFile(p);
            }
        }
        if (test && frames < 2) SDL_Delay(1);
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        shortcuts();
        tickCountdown();
        const double now = Util::now(), dt = now - last;
        last = now;
        if (st.playing && !st.baked.empty()) {
            st.playhead += dt;
            if (st.playhead > st.baked.duration()) st.playhead = 0;
        }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        menuBar();
        const float statusH = ImGui::GetFrameHeight() + 8;
        dockspace(vp, statusH);
        rebake();
        drawPanels();
        statusBar(vp, statusH);
        prefsWindow();
        if (askModels) { ImGui::OpenPopup("##models"); askModels = false; }
        ImGui::SetNextWindowSize(ImVec2(520, 0));
        if (ImGui::BeginPopupModal("##models", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
            ImGui::PushFont(fontBold); ImGui::TextUnformatted(T("Pantomimus needs its pose models")); ImGui::PopFont();
            long long bytes = 0;
            for (const auto& m : Download::missing(st.modelsDir(), st.set.pose.model, st.set.hands.on, st.set.face.on)) bytes += m.size;
            ImGui::TextWrapped(T("BlazePose (MediaPipe, Apache-2.0): about %.0f MB from Hugging Face, into %s. They're fetched once."), bytes / 1e6, st.modelsDir().c_str());
            const float p = downloadProgress();
            if (p >= 0) { ImGui::ProgressBar(p, ImVec2(-1, 0)); }
            else {
                const std::string ds = downloadStatus();
                if (!ds.empty()) ImGui::TextWrapped("%s", ds.c_str());
                if (missingModels().empty()) { if (button("Close", 120)) ImGui::CloseCurrentPopup(); }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
                    if (button("Download", 120)) downloadModels();
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (button("Not now", 120)) ImGui::CloseCurrentPopup();
                    ImGui::SameLine();
                    if (button("Preferences...", 140)) { ImGui::CloseCurrentPopup(); st.showPrefs = true; }
                }
            }
            ImGui::EndPopup();
        }
        if (st.showAbout) { ImGui::OpenPopup("##about"); st.showAbout = false; }
        ImGui::SetNextWindowSize(ImVec2(Splash::W, Splash::H));
        if (ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_NoDecoration)) {
            const ImVec2 o = ImGui::GetWindowPos();
            Splash::draw(ImGui::GetWindowDrawList(), o.x, o.y, T("Click to close"), -1);
            if (!scripted && (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Escape))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (changed) { changed = false; settingsChanged(); if (!test) saveSettings(); }
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0x1a / 255.0f, 0x1a / 255.0f, 0x1a / 255.0f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (scripted) {
            const bool done = Script::tick();
            if (done || frames > 20000) {
                if (!capture.empty()) {
                    std::vector<unsigned char> px((size_t)w * h * 4), flip(px.size());
                    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
                    for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
                    Img::saveBmp(capture, w, h, flip.data());
                }
                if (frames > 20000) out("script: timed out");
                st.quit = true;
            }
        }
        SDL_GL_SwapWindow(window);
    }
    quitting = true;
    worker.join();
    dlCancel = true;
    if (downloader.joinable()) downloader.join();
    server.stop(); pyg.stop();
    if (!test) saveSettings();
    View3D::shutdown();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
