// Pantomimus - the app's state, shared by the window (gui.cpp), the test scripts (script.cpp) and the self-test.
#pragma once
#include "clip.h"
#include "export.h"
#include "filter.h"
#include "hands.h"
#include "image.h"
#include "link.h"
#include "pose.h"
#include "skeleton.h"
#include "solver.h"
#include "source.h"
#include "view3d.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace App {

// everything the user sets (saved in settings.ini)
struct Settings {
    Pose::Settings pose;
    Filter::Settings filter;
    Solver::Settings solver;
    Hands::Settings hands;
    Clip::Cleanup cleanup;
    Export::Options exportOpts;
    int exportFormat = 0;                // 0 FBX, 1 glTF binary, 2 BVH
    int camera = 0, camW = 1280, camH = 720;
    float camFps = 30;
    bool mirrorView = true;              // the camera's picture shown as a mirror
    bool overlay = true, overlayRoi = false, overlayLabels = false;
    bool tPose = false;                  // the skeleton's rest pose: T (else A, as measured)
    float height = 1.66f;                // the skeleton's height, metres
    int countdown = 3;                   // seconds before recording starts
    std::string modelsDir;               // "" : the default (Util::defaultModelsDir)
    std::string language = "en";
    bool openLast = false;               // open the last project at start
    bool cameraAtStart = false;          // start the camera at start
    std::string lastProject;
    bool linkOn = false;
    int linkPort = Link::DEFAULT_PORT;
    bool pygOn = false;
    std::string pygHost = "127.0.0.1";
    int pygPort = Link::PYGMALION_PORT;
    float pygRate = 8;
    View3D::Look look;
};

// what the tracking last made (the worker thread writes it, the window reads it)
struct Live {
    Image frame;
    long long frameNo = 0;
    double t = 0;
    Pose::Result raw, filtered;
    Hands::Result hands;
    Solver::Frame solved;
    float fps = 0;                       // tracked frames a second
    double ms = 0;                       // the models' time a frame
    std::string source;                  // what's showing
    std::string error;
    bool ended = false;                  // a video came to its end
    double videoPos = 0, videoLength = 0;
};

struct State {
    Settings set;
    Skel::Skeleton measured;             // as loaded (A pose, 1.66 m)
    Skel::Skeleton skel;                 // as used: A or T, at the height
    std::vector<Clip::Take> takes;
    int sel = -1;                        // the selected take
    Clip::Baked baked;                   // the selected take, made with the settings
    bool bakedDirty = true;
    double playhead = 0;                 // seconds into the baked take
    bool playing = false, showTake = false;
    std::string project;                 // the project's file ("" : not saved yet)
    bool modified = false;
    std::string status;                  // the status bar's message
    double statusTime = 0;
    bool showAbout = false, showPrefs = false, quit = false;
    std::string modelsDir() const;
};

extern State st;

// the worker: the source, the tracking, the recording
void openCamera(int index);
void openFile(const std::string& path);     // a picture or a video
void trackVideo(const std::string& path);   // every frame of a video into a new take
void closeSource();
bool sourceOpen();
Live live();                                // a copy of the latest
void startRecording(bool countdown);
void stopRecording();
bool recording();
float countdownLeft();                      // seconds, 0 when not counting down
void resetTracking();
void settingsChanged();                     // the worker takes the new settings
void skeletonChanged();                     // the rest pose or height changed

// takes and the project
void select(int take);
void deleteTake(int take);
void duplicateTake(int take);
void undo();
void redo();
void pushUndo(const char* what);
bool exportTake(int take, const std::string& path, std::string& err);
bool saveProject(const std::string& path, std::string& err);
bool openProject(const std::string& path, std::string& err);
void newProject();
void rebake();                              // the selected take made again (when settings changed)

void setLink(bool on);                      // the live link server
void setPygmalion(bool on);
int linkClients();
void say(const std::string& msg);           // the status bar
void out(const std::string& line);          // a line on stdout (tests)
void setLanguage(const std::string& code);
void saveSettings();

// the models: what's missing for the settings, and fetching them
std::vector<std::string> missingModels();
void downloadModels();
float downloadProgress();                   // < 0: not downloading
std::string downloadStatus();

}  // namespace App

namespace Script {
// the test hooks: --script NAME drives the window as a user would, one step a frame; true when it's done
bool start(const std::string& name, const std::string& source);
bool tick();
bool readyForCapture();
const char* names();
}

namespace SelfTest {
int run(const std::string& testsDir);       // the unit tests; returns the failures
}
