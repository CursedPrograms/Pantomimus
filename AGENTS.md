# Pantomimus: notes for coding agents

Pantomimus is Atelier's webcam motion capture: full-body poses from any webcam, in real time, cleaned into 3D
skeletal animation and exported (`.bvh`, `.fbx`, `.glb`) for Blender, Demiurge (Atelier's game engine), Unity,
Unreal, or put straight onto a Pygmalion character. Read README.md: it is the promise; build what it says.
Repo: https://github.com/CursedPrograms/Pantomimus (MIT). Owner: Cursed Entertainment.

## The stack

C++17, CMake, SDL2 + OpenGL 3.3 + Dear ImGui, stb. Copy the setup (CMakeLists fetching SDL2/ImGui, splash, language
packs, build/install scripts) from the Atelier apps already here:
- `D:\cc\Mosaic-Mask\cpp`: webcam + face pipeline, splash (`src/splash.*`), `src/lang.*` + `lang/`, build.bat/.ps1/.sh,
  install.bat/.ps1/.sh.
- `D:\cc\Pygmalion`: a 3D viewer (skeleton drawing, MakeHuman's skeletons incl. "game engine" and CMU mocap, BVH
  import/export, FBX/glTF/Collada export with a skeleton: `src/skeleton.*`, `src/export.*`). Reuse its BVH/FBX/glTF
  writers and skeleton definitions rather than writing new ones.

Pose tracking: **MediaPipe BlazePose** (Apache-2.0), run in C++ with **ONNX Runtime** (MIT): the pose detector, then
the landmark model (lite/full/heavy as a setting), 33 landmarks with 3D world coordinates, ROI tracking between
frames as MediaPipe does. Get ONNX conversions of the official models (e.g. PINTO0309's model zoo, check its licence)
or convert the .tflite yourself; keep the models' licence files beside them. Models go in `F:\AIModels\pantomimus`
(look in F:\AIModels before downloading anything; C: is nearly full) and are downloaded on first run by the app.
Hands and face (MediaPipe Hands / Face Mesh) come after the body works.

Webcam: Media Foundation on Windows (V4L2 on Linux), camera picker, resolution and fps.

## What it does (in order; commit each step that builds and works)

1. The window: Atelier's look, splash, languages, the camera view with the tracked skeleton drawn over it, a 3D view
   of the skeleton beside it.
2. Tracking: BlazePose live, landmark smoothing (One Euro filter, every setting a slider), confidence handling.
3. Solving: landmarks to joint rotations on a standard humanoid (MakeHuman's game-engine skeleton from Pygmalion,
   Mixamo naming as an option), foot contact / ground lock, root motion.
4. Recording: record, trim, play back, scrub; clean-up (smoothing, foot locking, loop).
5. Export: BVH, FBX, glTF (`.glb`), with the skeleton; and a live link: stream to Blender (an add-on in the repo) and
   to Pygmalion (it has a socket server: `D:\cc\Pygmalion\src\socket.*`), later Demiurge.
6. Video files as input as well as the webcam; hands and face.
7. **MetaHuman characters** (added by the user): use Epic's **OpenRigLogic** (https://github.com/EpicGames/OpenRigLogic,
   MIT; clone the `5.8` branch into `external/`) to load a MetaHuman's DNA files (head + body, from MetaHuman
   Creator's Export > DCC Export, the user brings their own) and drive them: the body from the solved skeleton, the
   face from face tracking (MediaPipe Face Mesh / face blendshapes mapped onto the rig's face controls, evaluated with
   RigLogic: joints, blend shapes, animated-map weights). Record and export the face animation too. Trademarks: say
   "loads MetaHuman DNA files" / "uses OpenRigLogic" only to describe; never put MetaHuman, RigLogic or OpenRigLogic
   in a product or feature name, and don't ship Epic's sample characters (Fab licence).

Every feature gets a test hook: `Pantomimus.exe --script NAME --capture shot.bmp` (drives it as a user would,
prints a line, saves a screenshot, quits), and a `--selftest` of unit tests. Build, run them, look at the
screenshot, then commit.

## The user's rules for every Atelier app

- **Look:** Changeling greys (`#1a1a1a` bars, `#242424` panels, `#141414` wells, `#e6e6e6` text), controls in the
  **Atelier blue `#007bb7`** (not pink), solid colours only: no gradients, no shadows. Full spec:
  `D:\cc\Atelier\DESIGN.md` (follow it; Diorama is the reference).
- **Controls in a panel on the left**, the work in the middle, lists on the right. Every panel resizable (drag
  handle, width remembered).
- **Every parameter is a slider.**
- **Opens empty**; "open last project at start" is a Preferences option, off by default.
- **Splash screen** in the LayerTack style used across Atelier (see Mosaic-Mask / Pygmalion splash).
- **12 languages:** English plus Japanese, Chinese, Spanish, Korean, French, German, Russian, Serbian, Afrikaans,
  Yucatec Maya and Pennsylvania Dutch (`lang/` packs).
- **Feature-rich:** port everything the original ideas have and add the obvious extras.
- Every `.bat` gets a matching `.sh` and `.ps1`. Installed to `%LOCALAPPDATA%\Programs\Pantomimus`; Start Menu and
  desktop shortcuts named **"Atelier - Pantomimus"**.
- Repo files: GitHub Pages site (`index.html` + `site/style.css` + `site/site.js`, never inline CSS/JS), the Cursed
  README footer (as it is now), `.github/FUNDING.yml` (copy KIDA-01's / Pygmalion's), `link.bf`, repo topics.
- Credit line, exactly: "Farica☆Kimora © Cursed Entertainment" where a credit is shown.
- Don't name other products' or old names in the app; credit licences as they require.
- Ports: 5000-5999 belong to DREAM Robotics; use ports outside that range.
- Work only in `D:\cc\Pantomimus` (and read-only from the folders named above).
- Commit with a clear message and push to `main` day to day (`git push`); no GitHub release unless the user asks.
  Commit messages end with a blank line and the Co-Authored-By line for your model.
- The app's logo: leave a placeholder; the user paints the logos.
