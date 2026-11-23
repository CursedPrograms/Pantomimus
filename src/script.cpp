// Pantomimus - the test hooks: `Pantomimus --script NAME [--source FILE] [--capture shot.bmp]` drives the window as a
// user would (one step a frame), prints a line saying what came of it, and the window saves a screenshot and quits.
#include "app.h"
#include "lang.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <thread>

namespace Script {

using namespace App;

namespace {
using Step = std::function<bool()>;   // true: done, on to the next
std::vector<Step> steps;
size_t at = 0;
int frame = 0, stepFrame = 0;
double stepStart = 0;
std::string src;
std::string linkLine;
std::thread linkReader;

std::string fmt(const char* f, ...) {
    char b[1024];
    va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    return b;
}
Step wait(int frames) { return [frames] { return stepFrame >= frames; }; }
Step once(std::function<void()> f) { return [f] { f(); return true; }; }
// until the tracking has someone (at most 60 s)
Step tracked(int minFrames = 10) {
    return [minFrames] {
        const Live L = live();
        if (L.filtered.found && L.frameNo >= minFrames) return true;
        if (Util::now() - stepStart > 60) { out("script: no one was tracked (" + (L.error.empty() ? std::string("no error") : L.error) + ")"); return true; }
        return false;
    };
}
double angle(const M::V3& a, const M::V3& b) { return std::acos(std::clamp(M::dot(M::normalize(a), M::normalize(b)), -1.0f, 1.0f)) * 180 / M::PI; }

void openSource() {
    if (src.empty()) { out("script: give a --source picture or video"); return; }
    openFile(src);
}
void recordSome(int frames) {
    steps.push_back(once([] { startRecording(false); }));
    steps.push_back([frames] { return (int)live().frameNo >= frames + 12 || Util::now() - stepStart > 60; });
    steps.push_back(once([] { stopRecording(); }));
    steps.push_back(wait(3));
}
std::string exportAll(const std::string& stem) {
    std::string r;
    for (const char* e : {".bvh", ".glb", ".fbx"}) {
        const std::string p = Util::join(Util::tempDir(), stem + e);
        std::string err;
        const bool ok = exportTake(st.sel, p, err);
        r += fmt(" %s=%s", e + 1, ok ? fmt("%lld bytes", Util::fileSize(p)).c_str() : err.c_str());
    }
    return r;
}
}  // namespace

const char* names() { return "window, about, prefs, lang, track, solve, record, takes, export, link, video, hands, face, markers"; }

bool start(const std::string& name, const std::string& source) {
    steps.clear(); at = 0; frame = 0; stepFrame = 0; stepStart = Util::now(); src = source;
    if (name == "window") {
        steps.push_back(wait(20));
        steps.push_back(once([] { out(fmt("window: ok, %d x %d skeleton bones, %zu takes, models in %s", (int)st.skel.bones.size(), (int)st.skel.bones.size(), st.takes.size(), st.modelsDir().c_str())); }));
        if (!source.empty()) { openSource(); steps.insert(steps.begin(), tracked()); }
    } else if (name == "about") {
        steps.push_back(once([] { st.showAbout = true; }));
        steps.push_back(wait(10));
        steps.push_back(once([] { out("about: the splash shows in Help > About"); }));
    } else if (name == "prefs") {
        steps.push_back(once([] { st.showPrefs = true; }));
        steps.push_back(wait(10));
        steps.push_back(once([] { out(fmt("prefs: models %s, %zu missing", st.modelsDir().c_str(), missingModels().size())); }));
    } else if (name == "lang") {
        steps.push_back(once([] { setLanguage("ja"); }));
        steps.push_back(wait(10));
        steps.push_back(once([] { out(fmt("lang: %s, \"File\" is \"%s\"", Lang::current().c_str(), T("File"))); }));
    } else if (name == "track") {
        openSource();
        steps.push_back(tracked());
        steps.push_back(wait(5));
        steps.push_back(once([] {
            const Live L = live();
            const Pose::Result& r = L.filtered;
            out(fmt("track: %s score=%.3f nose=(%.3f,%.3f) hips=(%.3f,%.3f) ankles=(%.3f,%.3f)/(%.3f,%.3f) %.1f ms, %s", r.found ? "found" : "NOT FOUND", r.score,
                    r.image[0].x, r.image[0].y, (r.image[23].x + r.image[24].x) / 2, (r.image[23].y + r.image[24].y) / 2,
                    r.image[27].x, r.image[27].y, r.image[28].x, r.image[28].y, L.ms, L.source.c_str()));
        }));
    } else if (name == "solve") {
        openSource();
        steps.push_back(tracked(20));
        steps.push_back(once([] {
            const Live L = live();
            Skel::World w; Skel::forward(st.skel, L.solved.pose, w);
            const Skel::Roles ro = Skel::roles(st.skel);
            auto dir = [&](int b) { return w.tail[b] - w.head[b]; };
            out(fmt("solve: upper arms %.0f/%.0f deg from down, elbows %.0f/%.0f deg, knees %.0f/%.0f deg, hips at %.2f m, feet planted %d/%d",
                    angle(dir(ro.upperarm[0]), M::V3(0, -1, 0)), angle(dir(ro.upperarm[1]), M::V3(0, -1, 0)),
                    angle(dir(ro.upperarm[0]), dir(ro.lowerarm[0])), angle(dir(ro.upperarm[1]), dir(ro.lowerarm[1])),
                    angle(dir(ro.thigh[0]), dir(ro.calf[0])), angle(dir(ro.thigh[1]), dir(ro.calf[1])),
                    w.head[st.skel.hips].y, (int)L.solved.contact[0], (int)L.solved.contact[1]));
        }));
    } else if (name == "record") {
        openSource();
        steps.push_back(tracked());
        recordSome(45);
        steps.push_back(once([] {
            if (st.sel < 0) { out("record: no take was made"); return; }
            Clip::Take& t = st.takes[st.sel];
            t.in = 0.2; t.out = std::min(t.length(), 1.2); st.bakedDirty = true;
            st.playing = true; st.showTake = true;
        }));
        steps.push_back(wait(30));
        steps.push_back(once([] {
            st.playing = false;
            const Clip::Take& t = st.takes[st.sel];
            out(fmt("record: %s, %zu frames over %.2f s, trimmed to %.2f-%.2f s, baked %zu frames at %.0f fps, playhead %.2f s",
                    t.name.c_str(), t.frames(), t.length(), t.in, t.outPoint(), st.baked.frames.size(), st.baked.fps, st.playhead));
        }));
    } else if (name == "takes") {
        openSource();
        steps.push_back(tracked());
        recordSome(15);
        recordSome(15);
        steps.push_back(once([] {
            const size_t n = st.takes.size();
            duplicateTake(0);
            deleteTake(1);
            const size_t afterDelete = st.takes.size();
            undo();
            const size_t afterUndo = st.takes.size();
            redo();
            std::string err;
            const std::string p = Util::join(Util::tempDir(), "script.pantomimus");
            const bool saved = saveProject(p, err);
            newProject();
            const bool opened = openProject(p, err);
            out(fmt("takes: %zu recorded, %zu after duplicate+delete, %zu after undo, %zu after redo; project saved %s, opened %s with %zu takes",
                    n, afterDelete, afterUndo, st.takes.size(), saved ? "yes" : err.c_str(), opened ? "yes" : err.c_str(), st.takes.size()));
        }));
    } else if (name == "export") {
        openSource();
        steps.push_back(tracked());
        recordSome(30);
        steps.push_back(once([] {
            if (st.sel < 0) { out("export: no take"); return; }
            std::string r = exportAll("pantomimus_test");
            st.set.exportOpts.naming = Skel::NAME_MIXAMO;
            r += " | Mixamo:" + exportAll("pantomimus_test_mixamo");
            st.set.exportOpts.naming = Skel::NAME_OWN;
            out("export:" + r + " in " + Util::tempDir());
        }));
    } else if (name == "link") {
        openSource();
        steps.push_back(tracked());
        steps.push_back(once([] {
            setLink(true);
            linkReader = std::thread([] { std::string err; if (!Link::readLine("127.0.0.1", st.set.linkPort, linkLine, 5000, err)) linkLine = "ERROR " + err; });
        }));
        steps.push_back([] { return !linkReader.joinable() || Util::now() - stepStart > 1; });
        steps.push_back([] {
            if (linkReader.joinable()) linkReader.join();
            const size_t bones = std::count(linkLine.begin(), linkLine.end(), '{') - 1;
            out(fmt("link: port %d, first line %.40s... (%zu bones)", st.set.linkPort, linkLine.c_str(), bones));
            setLink(false);
            return true;
        });
    } else if (name == "video") {
        steps.push_back(once([] { if (src.empty()) out("video: give a --source video"); else trackVideo(src); }));
        steps.push_back([] { return !st.takes.empty() || Util::now() - stepStart > 600; });
        steps.push_back(wait(5));
        steps.push_back(once([] {
            if (st.takes.empty()) { out("video: no take came"); return; }
            const Clip::Take& t = st.takes.back();
            size_t found = 0; for (const auto& r : t.raw) found += r.found;
            out(fmt("video: %s, %zu frames over %.2f s, someone in %zu of them", t.name.c_str(), t.frames(), t.length(), found));
        }));
    } else if (name == "hands") {
        steps.push_back(once([] {
            st.set.hands.on = true; settingsChanged();
            if (!missingModels().empty()) downloadModels();
        }));
        steps.push_back([] { return downloadProgress() < 0 && missingModels().empty() || Util::now() - stepStart > 300; });
        steps.push_back(once([] { openSource(); }));
        steps.push_back(tracked(15));
        steps.push_back(wait(10));
        steps.push_back(once([] {
            const Live L = live();
            out(fmt("hands: left %s (%.2f), right %s (%.2f)", L.hands.hand[0].found ? "found" : "not found", L.hands.hand[0].score,
                    L.hands.hand[1].found ? "found" : "not found", L.hands.hand[1].score));
        }));
    } else if (name == "face" || name == "markers") {
        // the face (a close-up works: no body needed); "markers": with Sharpie dots, calibrated, then tracked and recorded
        const bool marks = name == "markers";
        steps.push_back(once([marks] {
            st.set.face.on = true; st.set.markers.on = marks; st.set.markerGuide = true; settingsChanged();
            if (marks) clearMarkers();
            if (!missingModels().empty()) downloadModels();
        }));
        steps.push_back([] { return downloadProgress() < 0 && missingModels().empty() || Util::now() - stepStart > 300; });
        steps.push_back(once([] { openSource(); }));
        steps.push_back([] { const Live L = live(); return (L.face.found && L.frameNo > 5) || Util::now() - stepStart > 60; });
        if (marks) {
            steps.push_back(once([] { calibrateMarkers(); }));
            steps.push_back(wait(8));
            recordSome(20);
        } else steps.push_back(wait(8));
        steps.push_back(once([marks] {
            const Live L = live();
            const Face::Result& f = L.face;
            out(fmt("face: %s (%.2f), smile %.2f, jawOpen %.2f, blink %.2f, yaw %.0f pitch %.0f roll %.0f", f.found ? "found" : "not found", f.score,
                    (f.weights[44] + f.weights[45]) / 2, f.weights[25], (f.weights[9] + f.weights[10]) / 2, f.yaw, f.pitch, f.roll));
            if (marks) {
                out(fmt("markers: %s, %d of %d dots tracked", L.markers.calibrated ? "calibrated" : "not calibrated", L.markers.found, Markers::COUNT));
                if (st.sel >= 0) {
                    const Clip::Take& t = st.takes[st.sel];
                    std::string err;
                    const std::string a = Util::join(Util::tempDir(), "pantomimus_test_face.csv"), b = Util::join(Util::tempDir(), "pantomimus_test_markers.csv");
                    const bool okA = Clip::exportFaceCsv(t, 30, a, err), okB = Clip::exportMarkersCsv(t, b, err);
                    out(fmt("take: %zu frames, %zu faces, %zu marker frames; face CSV %s (%lld bytes), markers CSV %s (%lld bytes)", t.frames(), t.faces.size(),
                            t.markers.size(), okA ? "ok" : err.c_str(), Util::fileSize(a), okB ? "ok" : err.c_str(), Util::fileSize(b)));
                }
            }
        }));
    } else return false;
    return true;
}

bool tick() {
    ++frame; ++stepFrame;
    while (at < steps.size()) {
        if (!steps[at]()) return false;
        ++at; stepFrame = 0; stepStart = Util::now();
    }
    return true;
}

bool readyForCapture() { return at >= steps.size(); }

}  // namespace Script
