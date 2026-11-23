// Pantomimus - the humanoid skeleton the motion goes onto: its bones (from data/skeletons/*.skel: the game-engine
// skeleton measured on MakeHuman's body), its rest pose (A as measured, or T), names (its own or Mixamo's), and a pose:
// a rotation per bone relative to its parent plus where the root is. Bones' rest frames are the world's axes (Y up,
// the body facing +Z, its left at +X), so a pose's rotations read the same in every program.
#pragma once
#include "mathx.h"

#include <map>
#include <string>
#include <vector>

namespace Skel {

using M::Q;
using M::V3;

struct Bone {
    std::string name;      // the skeleton's own name (pelvis, thigh_l...)
    int parent = -1;
    V3 head, tail;         // rest, metres, world
};

struct Skeleton {
    std::string name;
    std::vector<Bone> bones;   // parents before children
    std::map<std::string, int> byName;
    int hips = -1;             // the bone the body hangs from (pelvis): a pose moves it up and down (Pose::hips)
    int find(const std::string& n) const { auto it = byName.find(n); return it == byName.end() ? -1 : it->second; }
    bool empty() const { return bones.empty(); }
    float height() const;      // the top of the head over the lowest joint
};

bool load(Skeleton& s, const std::string& path, std::string& err);
// the T pose: the arms out level (the clavicles kept), from the measured A pose
Skeleton toTPose(const Skeleton& a);
// the skeleton scaled (a performer's height, or units)
Skeleton scaled(const Skeleton& s, float k);

// names: 0 the skeleton's own (game engine), 1 Mixamo ("mixamorig:Hips"), 2 Mixamo without the prefix
enum Naming { NAME_OWN = 0, NAME_MIXAMO = 1, NAME_MIXAMO_BARE = 2 };
std::string nameOf(const Skeleton& s, int bone, int naming);
// with Mixamo's names the Root bone goes (the hips are the root); true: this bone is written
bool written(const Skeleton& s, int bone, int naming);

struct Pose {
    std::vector<Q> local;   // per bone, relative to the parent (identity: as at rest)
    V3 root;                // the root bone's head, metres: on the floor under the hips (see Solver)
    V3 hips;                // the hips moved from their rest place over the root (crouching, jumping), metres
    void reset(const Skeleton& s) { local.assign(s.bones.size(), Q()); root = s.bones.empty() ? V3() : s.bones[0].head; hips = V3(); }
};

// the posed bones in the world: each bone's turn and its head and tail
struct World {
    std::vector<Q> rot;
    std::vector<V3> head, tail;
};
void forward(const Skeleton& s, const Pose& p, World& w);
// a pose's local rotations from world rotations (the inverse of forward's rotations)
void localFromWorld(const Skeleton& s, const std::vector<Q>& worldRot, Pose& p);

// the bones the tracking drives, by role (indices into the skeleton, -1 when it hasn't the bone)
struct Roles {
    int root = -1, pelvis = -1, spine[3] = {-1, -1, -1}, neck = -1, head = -1;
    int clavicle[2] = {-1, -1}, upperarm[2] = {-1, -1}, lowerarm[2] = {-1, -1}, hand[2] = {-1, -1};
    int thigh[2] = {-1, -1}, calf[2] = {-1, -1}, foot[2] = {-1, -1}, ball[2] = {-1, -1};
    int finger[2][5][3];   // thumb, index, middle, ring, pinky; 3 bones each ([side][finger][segment])
    Roles();
};
Roles roles(const Skeleton& s);   // [0] left, [1] right

}  // namespace Skel
