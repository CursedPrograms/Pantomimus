// Pantomimus - the 3D view: the skeleton (a plain mannequin on its bones), the floor, the planted feet and the world
// landmarks, drawn with OpenGL 3.3 into a texture the window shows. Drag to turn, right-drag to move, wheel to zoom.
#pragma once
#include "face.h"
#include "mathx.h"
#include "pose.h"
#include "skeleton.h"

namespace View3D {

struct Camera {
    float yaw = 0.5f, pitch = 0.18f, dist = 3.6f;
    M::V3 target{0, 0.9f, 0};
    void reset() { *this = Camera(); }
};

struct Look {
    bool mannequin = true, bones = true, floor = true, landmarks = false, follow = true, face = true;
};

bool init();   // after the GL context is made
void shutdown();
// draws into its own texture, w x h; returns the texture (0 on failure). face (may be null): a shaded avatar head
// built from the tracked landmarks; aspect is the camera frame's width/height, to keep the head's proportions
unsigned render(int w, int h, const Camera& cam, const Look& look, const Skel::Skeleton& s, const Skel::Pose* pose,
                const bool contact[2], const Pose::Result* landmarks, bool mirror,
                const Face::Result* face = nullptr, float aspect = 1.0f);
// mouse on the view: drags in pixels, wheel steps
void orbit(Camera& c, float dx, float dy);
void pan(Camera& c, float dx, float dy, int h);
void zoom(Camera& c, float steps);

}  // namespace View3D
