// Pantomimus - the exports: Biovision (.bvh), glTF binary (.glb) and Filmbox (.fbx, binary 7.4), each the skeleton with
// the take's motion. The bones' rest frames are the world's axes, so every program reads the rotations the same way.
// glTF and FBX can carry a plain mannequin skinned to the bones (glTF needs a mesh for most programs to make an armature).
#pragma once
#include "clip.h"
#include "skeleton.h"

#include <string>
#include <vector>

namespace Export {

struct Options {
    int naming = Skel::NAME_OWN;   // the bones' names (the skeleton's own, Mixamo's)
    float scale = 1;               // metres times this (BVH: 100 makes centimetres)
    bool mannequin = true;         // glTF / FBX: a simple body skinned to the bones
    int zUp = 0;                   // BVH: 0 Y up (the usual), 1 Z up
    std::string takeName = "Take";
};

bool saveBvh(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err);
bool saveGlb(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err);
bool saveFbx(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err);
// by the file's extension
bool save(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err);

// one pose as a one-frame BVH (the live link to programs that read BVH poses)
bool poseBvh(const Skel::Skeleton& s, const Skel::Pose& p, const std::string& path, const Options& o, std::string& err);

// the mannequin: a tapered box along each bone (head to tail), each vertex on one bone
struct Mesh {
    std::vector<float> pos, nrm;   // xyz per vertex, rest
    std::vector<int> bone;         // per vertex
    std::vector<unsigned> tri;
};
Mesh mannequin(const Skel::Skeleton& s, float scale);

// the written bones (Mixamo's names drop the root) and, per written bone, its written parent (-1: a root)
struct Layout {
    std::vector<int> bones;        // skeleton indices, parents first
    std::vector<int> parent;       // index into bones
    std::vector<std::string> names;
};
Layout layout(const Skel::Skeleton& s, int naming, bool dropRoot = false);   // dropRoot: the hips become the root (BVH)
// a written bone's place and turn relative to its written parent, for a pose
void localOf(const Skel::Skeleton& s, const Layout& L, const Skel::World& w, int k, M::V3& t, M::Q& r);

}  // namespace Export
