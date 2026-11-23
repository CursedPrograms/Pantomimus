// Pantomimus - the exports (see export.h). Written for Pantomimus: BVH as Biovision describes it, glTF 2.0 binary as
// Khronos specifies it, FBX 7.4 binary as Blender's and the FBX SDK's readers take it.
#include "export.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace Export {

using M::Q;
using M::V3;

Layout layout(const Skel::Skeleton& s, int naming, bool dropRoot) {
    Layout L;
    std::vector<int> at(s.bones.size(), -1);
    for (size_t i = 0; i < s.bones.size(); ++i) {
        if (!Skel::written(s, (int)i, dropRoot ? Skel::NAME_MIXAMO : naming)) continue;
        int p = s.bones[i].parent;
        while (p >= 0 && at[p] < 0) p = s.bones[p].parent;
        at[i] = (int)L.bones.size();
        L.bones.push_back((int)i);
        L.parent.push_back(p >= 0 ? at[p] : -1);
        L.names.push_back(Skel::nameOf(s, (int)i, naming));
    }
    return L;
}

void localOf(const Skel::Skeleton& s, const Layout& L, const Skel::World& w, int k, V3& t, Q& r) {
    const int i = L.bones[k];
    if (L.parent[k] < 0) { t = w.head[i]; r = w.rot[i]; return; }
    const int j = L.bones[L.parent[k]];
    const Q inv = M::conj(w.rot[j]);
    t = M::rotate(inv, w.head[i] - w.head[j]);
    r = M::normalize(inv * w.rot[i]);
    (void)s;
}

static V3 restLocal(const Skel::Skeleton& s, const Layout& L, int k) {
    const int i = L.bones[k];
    return L.parent[k] < 0 ? s.bones[i].head : s.bones[i].head - s.bones[L.bones[L.parent[k]]].head;
}

// which written bones move (a translation channel): the roots and the hips
static bool moves(const Skel::Skeleton& s, const Layout& L, int k) { return L.parent[k] < 0 || L.bones[k] == s.hips; }

// angles kept continuous from frame to frame (no jumps of 360)
static void unwrap(std::vector<V3>& e) {
    for (size_t i = 1; i < e.size(); ++i)
        for (int c = 0; c < 3; ++c) {
            float d = e[i][c] - e[i - 1][c];
            while (d > 180) { e[i][c] -= 360; d -= 360; }
            while (d < -180) { e[i][c] += 360; d += 360; }
        }
}

static const float DEG = 180.0f / M::PI;

// ---------------------------------------------------------------- BVH
static bool writeBvh(const Skel::Skeleton& s, const std::vector<Skel::Pose>& poses, float frameTime, const std::string& path, const Options& o, std::string& err) {
    // the BVH's root is the hips (a Root bone on the floor goes: BVH readers move only the root)
    Options oo = o;
    const Layout L = layout(s, o.naming, true);
    const Q zq = M::axisAngle(V3(1, 0, 0), M::PI / 2);   // Y up to Z up
    auto axes = [&](const V3& v) { return o.zUp ? M::rotate(zq, v) : v; };
    auto axesQ = [&](const Q& q) { return o.zUp ? M::normalize(zq * q * M::conj(zq)) : q; };
    std::ofstream f(fs::u8path(path));
    if (!f) { err = "Can't write " + path; return false; }
    f.setf(std::ios::fixed); f.precision(6);
    f << "HIERARCHY\n";
    std::vector<std::vector<int>> kids(L.bones.size());
    for (size_t k = 0; k < L.bones.size(); ++k) if (L.parent[k] >= 0) kids[L.parent[k]].push_back((int)k);
    std::vector<int> order;
    std::function<void(int, int)> joint = [&](int k, int depth) {
        const std::string pad(depth * 2, ' ');
        const V3 off = axes(restLocal(s, L, k)) * o.scale;
        f << pad << (L.parent[k] < 0 ? "ROOT " : "JOINT ") << L.names[k] << "\n" << pad << "{\n";
        f << pad << "  OFFSET " << off.x << " " << off.y << " " << off.z << "\n";
        if (L.parent[k] < 0) f << pad << "  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n";
        else f << pad << "  CHANNELS 3 Zrotation Xrotation Yrotation\n";
        order.push_back(k);
        if (kids[k].empty()) {
            const Skel::Bone& b = s.bones[L.bones[k]];
            const V3 end = axes(b.tail - b.head) * o.scale;
            f << pad << "  End Site\n" << pad << "  {\n" << pad << "    OFFSET " << end.x << " " << end.y << " " << end.z << "\n" << pad << "  }\n";
        }
        for (int c : kids[k]) joint(c, depth + 1);
        f << pad << "}\n";
    };
    for (size_t k = 0; k < L.bones.size(); ++k) if (L.parent[k] < 0) joint((int)k, 0);
    f << "MOTION\nFrames: " << poses.size() << "\nFrame Time: " << frameTime << "\n";
    std::vector<std::vector<V3>> eul(L.bones.size(), std::vector<V3>(poses.size()));
    std::vector<std::vector<V3>> pos(L.bones.size(), std::vector<V3>(poses.size()));
    for (size_t fi = 0; fi < poses.size(); ++fi) {
        Skel::World w; Skel::forward(s, poses[fi], w);
        for (size_t k = 0; k < L.bones.size(); ++k) {
            V3 t; Q r; localOf(s, L, w, (int)k, t, r);
            pos[k][fi] = axes(t) * o.scale;
            eul[k][fi] = M::toEulerZXY(axesQ(r)) * DEG;
        }
    }
    for (auto& e : eul) unwrap(e);
    for (size_t fi = 0; fi < poses.size(); ++fi) {
        for (int k : order) {
            if (L.parent[k] < 0) f << pos[k][fi].x << " " << pos[k][fi].y << " " << pos[k][fi].z << " ";
            f << eul[k][fi].z << " " << eul[k][fi].x << " " << eul[k][fi].y << " ";
        }
        f << "\n";
    }
    (void)oo;
    if (!f) { err = "Couldn't write all of " + path; return false; }
    return true;
}

bool saveBvh(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err) {
    std::vector<Skel::Pose> poses;
    for (const auto& f : b.frames) poses.push_back(f.pose);
    if (poses.empty()) { Skel::Pose p; p.reset(s); poses.push_back(p); }
    return writeBvh(s, poses, 1.0f / b.fps, path, o, err);
}

bool poseBvh(const Skel::Skeleton& s, const Skel::Pose& p, const std::string& path, const Options& o, std::string& err) {
    return writeBvh(s, {p}, 1.0f / 30, path, o, err);
}

// ---------------------------------------------------------------- the mannequin
Mesh mannequin(const Skel::Skeleton& s, float scale) {
    Mesh m;
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const Skel::Bone& b = s.bones[i];
        if (b.parent < 0 && b.name == "Root") continue;
        const V3 d = b.tail - b.head;
        const float len = M::length(d);
        if (len < 1e-4f) continue;
        const V3 y = d / len;
        const V3 x = M::normalize(M::cross(std::fabs(y.z) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0), y));
        const V3 z = M::cross(x, y);
        // an octahedron-like bone: narrow at the joints, widest a fifth of the way along
        const float r = std::clamp(len * 0.16f, 0.004f, 0.07f);
        const V3 top = b.tail, bot = b.head, mid = b.head + y * (len * 0.2f);
        const V3 ring[4] = {mid + x * r, mid + z * r, mid - x * r, mid - z * r};
        const int tris[8][3] = {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}, {1, 0, 5}, {2, 1, 5}, {3, 2, 5}, {0, 3, 5}};
        const V3 pts[6] = {ring[0], ring[1], ring[2], ring[3], top, bot};
        for (const auto& t : tris) {
            const V3 a = pts[t[0]], bb = pts[t[1]], c = pts[t[2]];
            V3 n = M::normalize(M::cross(bb - a, c - a));
            // facing out from the bone's axis
            if (M::dot(n, (a + bb + c) / 3.0f - (bot + top) * 0.5f) < 0) n = -n;
            const V3 vs[3] = {a, bb, c};
            const bool flip = M::dot(M::cross(bb - a, c - a), n) < 0;
            for (int k = 0; k < 3; ++k) {
                const V3 v = vs[flip ? 2 - k : k] * scale;
                m.pos.insert(m.pos.end(), {v.x, v.y, v.z});
                m.nrm.insert(m.nrm.end(), {n.x, n.y, n.z});
                m.bone.push_back((int)i);
                m.tri.push_back((unsigned)m.tri.size());
            }
        }
    }
    return m;
}

// ---------------------------------------------------------------- glTF binary
namespace {
struct Bin {
    std::vector<uint8_t> data;
    std::ostringstream views, accessors;
    int nViews = 0, nAcc = 0;
    void align() { while (data.size() % 4) data.push_back(0); }
    int view(const void* p, size_t n, int target) {
        align();
        const size_t off = data.size();
        data.insert(data.end(), (const uint8_t*)p, (const uint8_t*)p + n);
        if (nViews) views << ",";
        views << "{\"buffer\":0,\"byteOffset\":" << off << ",\"byteLength\":" << n;
        if (target) views << ",\"target\":" << target;
        views << "}";
        return nViews++;
    }
    int accessor(int view, int comp, size_t count, const char* type, const std::string& minmax = "") {
        if (nAcc) accessors << ",";
        accessors << "{\"bufferView\":" << view << ",\"componentType\":" << comp << ",\"count\":" << count << ",\"type\":\"" << type << "\"" << minmax << "}";
        return nAcc++;
    }
};
std::string jsonStr(const std::string& s) {
    std::string r = "\"";
    for (char c : s) { if (c == '"' || c == '\\') r.push_back('\\'); r.push_back(c); }
    return r + "\"";
}
std::string num(double v) { char b[32]; snprintf(b, sizeof b, "%.7g", std::isfinite(v) ? v : 0.0); return b; }
}  // namespace

bool saveGlb(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err) {
    const Layout L = layout(s, o.naming);
    const size_t nb = L.bones.size();
    Bin bin;
    std::ostringstream nodes, meshes, skins, anims;
    // the joints: nodes in the rest pose
    std::vector<std::vector<int>> kids(nb);
    for (size_t k = 0; k < nb; ++k) if (L.parent[k] >= 0) kids[L.parent[k]].push_back((int)k);
    for (size_t k = 0; k < nb; ++k) {
        const V3 t = restLocal(s, L, (int)k) * o.scale;
        if (k) nodes << ",";
        nodes << "{\"name\":" << jsonStr(L.names[k]) << ",\"translation\":[" << num(t.x) << "," << num(t.y) << "," << num(t.z) << "]";
        if (!kids[k].empty()) { nodes << ",\"children\":["; for (size_t c = 0; c < kids[k].size(); ++c) nodes << (c ? "," : "") << kids[k][c]; nodes << "]"; }
        nodes << "}";
    }
    std::vector<int> sceneNodes;
    for (size_t k = 0; k < nb; ++k) if (L.parent[k] < 0) sceneNodes.push_back((int)k);
    int meshNode = -1;
    // the skin: inverse bind matrices are the rest heads moved back to the origin (rest frames are the world's axes)
    std::vector<float> ibm(nb * 16, 0);
    for (size_t k = 0; k < nb; ++k) {
        float* m = &ibm[k * 16];
        m[0] = m[5] = m[10] = m[15] = 1;
        const V3 h = s.bones[L.bones[k]].head * o.scale;
        m[12] = -h.x; m[13] = -h.y; m[14] = -h.z;
    }
    const int ibmAcc = bin.accessor(bin.view(ibm.data(), ibm.size() * 4, 0), 5126, nb, "MAT4");
    skins << "{\"name\":\"Armature\",\"inverseBindMatrices\":" << ibmAcc << ",\"joints\":[";
    for (size_t k = 0; k < nb; ++k) skins << (k ? "," : "") << k;
    skins << "]";
    if (!sceneNodes.empty()) skins << ",\"skeleton\":" << sceneNodes[0];
    skins << "}";
    if (o.mannequin) {
        const Mesh m = mannequin(s, o.scale);
        std::vector<int> jointOf(s.bones.size(), 0);
        for (size_t k = 0; k < nb; ++k) jointOf[L.bones[k]] = (int)k;
        // a bone that isn't written (the dropped root) gives its vertices to the nearest written parent
        for (size_t i = 0; i < s.bones.size(); ++i)
            if (!Skel::written(s, (int)i, o.naming)) { int p = s.bones[i].parent; while (p >= 0 && !Skel::written(s, p, o.naming)) p = s.bones[p].parent; jointOf[i] = p >= 0 ? jointOf[p] : 0; }
        const size_t nv = m.pos.size() / 3;
        std::vector<uint16_t> joints(nv * 4, 0);
        std::vector<float> weights(nv * 4, 0);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (size_t v = 0; v < nv; ++v) {
            joints[v * 4] = (uint16_t)jointOf[m.bone[v]]; weights[v * 4] = 1;
            for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], m.pos[v * 3 + c]); hi[c] = std::max(hi[c], m.pos[v * 3 + c]); }
        }
        const std::string mm = ",\"min\":[" + num(lo[0]) + "," + num(lo[1]) + "," + num(lo[2]) + "],\"max\":[" + num(hi[0]) + "," + num(hi[1]) + "," + num(hi[2]) + "]";
        const int aPos = bin.accessor(bin.view(m.pos.data(), m.pos.size() * 4, 34962), 5126, nv, "VEC3", mm);
        const int aNrm = bin.accessor(bin.view(m.nrm.data(), m.nrm.size() * 4, 34962), 5126, nv, "VEC3");
        const int aJ = bin.accessor(bin.view(joints.data(), joints.size() * 2, 34962), 5123, nv, "VEC4");
        const int aW = bin.accessor(bin.view(weights.data(), weights.size() * 4, 34962), 5126, nv, "VEC4");
        const int aI = bin.accessor(bin.view(m.tri.data(), m.tri.size() * 4, 34963), 5125, m.tri.size(), "SCALAR");
        meshes << "{\"name\":\"Mannequin\",\"primitives\":[{\"attributes\":{\"POSITION\":" << aPos << ",\"NORMAL\":" << aNrm << ",\"JOINTS_0\":" << aJ
               << ",\"WEIGHTS_0\":" << aW << "},\"indices\":" << aI << ",\"material\":0}]}";
        meshNode = (int)nb;
        nodes << ",{\"name\":\"Mannequin\",\"mesh\":0,\"skin\":0}";
        sceneNodes.push_back(meshNode);
    }
    // the animation: a rotation per joint, a translation for the roots and the hips
    if (!b.frames.empty()) {
        const size_t nf = b.frames.size();
        std::vector<float> times(nf);
        for (size_t f = 0; f < nf; ++f) times[f] = f / b.fps;
        const int aT = bin.accessor(bin.view(times.data(), times.size() * 4, 0), 5126, nf, "SCALAR", ",\"min\":[0],\"max\":[" + num(times.back()) + "]");
        std::vector<std::vector<float>> rot(nb, std::vector<float>(nf * 4)), tra(nb, std::vector<float>(nf * 3));
        std::vector<Q> prev(nb);
        for (size_t f = 0; f < nf; ++f) {
            Skel::World w; Skel::forward(s, b.frames[f].pose, w);
            for (size_t k = 0; k < nb; ++k) {
                V3 t; Q r; localOf(s, L, w, (int)k, t, r);
                if (f) r = M::sameHemisphere(r, prev[k]);
                prev[k] = r;
                rot[k][f * 4] = r.x; rot[k][f * 4 + 1] = r.y; rot[k][f * 4 + 2] = r.z; rot[k][f * 4 + 3] = r.w;
                t = t * o.scale;
                tra[k][f * 3] = t.x; tra[k][f * 3 + 1] = t.y; tra[k][f * 3 + 2] = t.z;
            }
        }
        std::ostringstream ch, sa;
        int n = 0;
        for (size_t k = 0; k < nb; ++k) {
            const int aR = bin.accessor(bin.view(rot[k].data(), rot[k].size() * 4, 0), 5126, nf, "VEC4");
            if (n) { ch << ","; sa << ","; }
            sa << "{\"input\":" << aT << ",\"output\":" << aR << ",\"interpolation\":\"LINEAR\"}";
            ch << "{\"sampler\":" << n << ",\"target\":{\"node\":" << k << ",\"path\":\"rotation\"}}";
            ++n;
            if (moves(s, L, (int)k)) {
                const int aTr = bin.accessor(bin.view(tra[k].data(), tra[k].size() * 4, 0), 5126, nf, "VEC3");
                ch << ","; sa << ",";
                sa << "{\"input\":" << aT << ",\"output\":" << aTr << ",\"interpolation\":\"LINEAR\"}";
                ch << "{\"sampler\":" << n << ",\"target\":{\"node\":" << k << ",\"path\":\"translation\"}}";
                ++n;
            }
        }
        anims << "{\"name\":" << jsonStr(o.takeName) << ",\"channels\":[" << ch.str() << "],\"samplers\":[" << sa.str() << "]}";
    }
    bin.align();
    std::ostringstream j;
    j << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Pantomimus " PANTOMIMUS_VERSION "\"},\"scene\":0,\"scenes\":[{\"name\":\"Scene\",\"nodes\":[";
    for (size_t k = 0; k < sceneNodes.size(); ++k) j << (k ? "," : "") << sceneNodes[k];
    j << "]}],\"nodes\":[" << nodes.str() << "]";
    if (o.mannequin)
        j << ",\"meshes\":[" << meshes.str() << "],\"materials\":[{\"name\":\"Mannequin\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.62,0.62,0.62,1],\"metallicFactor\":0,\"roughnessFactor\":0.7}}]";
    j << ",\"skins\":[" << skins.str() << "]";
    if (!anims.str().empty()) j << ",\"animations\":[" << anims.str() << "]";
    j << ",\"accessors\":[" << bin.accessors.str() << "],\"bufferViews\":[" << bin.views.str() << "],\"buffers\":[{\"byteLength\":" << bin.data.size() << "}]}";
    std::string js = j.str();
    while (js.size() % 4) js.push_back(' ');
    std::ofstream f(fs::u8path(path), std::ios::binary);
    if (!f) { err = "Can't write " + path; return false; }
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    u32(0x46546C67); u32(2); u32((uint32_t)(12 + 8 + js.size() + 8 + bin.data.size()));
    u32((uint32_t)js.size()); u32(0x4E4F534A); f.write(js.data(), js.size());
    u32((uint32_t)bin.data.size()); u32(0x004E4942); f.write((const char*)bin.data.data(), bin.data.size());
    if (!f) { err = "Couldn't write all of " + path; return false; }
    return true;
}

// ---------------------------------------------------------------- FBX 7.4 binary
namespace {
struct Prop {
    char type;
    std::string bytes;
};
struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> kids;
    Node(const std::string& n = "") : name(n) {}
    template <class T> void raw(char t, const T& v) { Prop p; p.type = t; p.bytes.assign((const char*)&v, sizeof v); props.push_back(p); }
    Node& I(int32_t v) { raw('I', v); return *this; }
    Node& L(int64_t v) { raw('L', v); return *this; }
    Node& D(double v) { raw('D', v); return *this; }
    Node& F(float v) { raw('F', v); return *this; }
    Node& C(bool v) { const char c = v ? 1 : 0; raw('C', c); return *this; }
    Node& S(const std::string& s) {
        Prop p; p.type = 'S';
        const uint32_t n = (uint32_t)s.size();
        p.bytes.assign((const char*)&n, 4); p.bytes += s;
        props.push_back(p); return *this;
    }
    Node& R(const std::string& s) { S(s); props.back().type = 'R'; return *this; }
    template <class T> Node& arr(char t, const std::vector<T>& v) {
        Prop p; p.type = t;
        const uint32_t n = (uint32_t)v.size(), enc = 0, len = (uint32_t)(v.size() * sizeof(T));
        p.bytes.append((const char*)&n, 4); p.bytes.append((const char*)&enc, 4); p.bytes.append((const char*)&len, 4);
        p.bytes.append((const char*)v.data(), len);
        props.push_back(p); return *this;
    }
    Node& add(const Node& k) { kids.push_back(k); return *this; }
    Node& child(const std::string& n) { kids.emplace_back(n); return kids.back(); }
};

void write(std::string& out, const Node& n) {
    const size_t start = out.size();
    out.append(12, '\0');
    out.push_back((char)n.name.size());
    out += n.name;
    const size_t propStart = out.size();
    for (const Prop& p : n.props) { out.push_back(p.type); out += p.bytes; }
    const uint32_t propLen = (uint32_t)(out.size() - propStart), count = (uint32_t)n.props.size();
    for (const Node& k : n.kids) write(out, k);
    if (!n.kids.empty() || n.props.empty()) out.append(13, '\0');
    const uint32_t end = (uint32_t)out.size();
    memcpy(&out[start], &end, 4); memcpy(&out[start + 4], &count, 4); memcpy(&out[start + 8], &propLen, 4);
}

// Properties70's entries
Node P(const std::string& name, const std::string& type, const std::string& label, const std::string& flags) {
    Node p("P"); p.S(name).S(type).S(label).S(flags); return p;
}
Node Pint(const std::string& n, int v) { Node p = P(n, "int", "Integer", ""); p.I(v); return p; }
Node Penum(const std::string& n, int v) { Node p = P(n, "enum", "", ""); p.I(v); return p; }
Node Pbool(const std::string& n, bool v) { Node p = P(n, "bool", "", ""); p.I(v ? 1 : 0); return p; }
Node Pdouble(const std::string& n, double v) { Node p = P(n, "double", "Number", ""); p.D(v); return p; }
Node Ptime(const std::string& n, int64_t v) { Node p = P(n, "KTime", "Time", ""); p.L(v); return p; }
Node Pstr(const std::string& n, const std::string& v) { Node p = P(n, "KString", "", ""); p.S(v); return p; }
Node Pvec(const std::string& n, const std::string& type, const V3& v, const std::string& flags = "A") {
    Node p = P(n, type, "", flags); p.D(v.x).D(v.y).D(v.z); return p;
}
Node Pcolor(const std::string& n, const V3& v) { Node p = P(n, "ColorRGB", "Color", ""); p.D(v.x).D(v.y).D(v.z); return p; }

std::string cls(const std::string& name, const std::string& c) { return name + std::string("\x00\x01", 2) + c; }
const int64_t KTIME = 46186158000LL;
}  // namespace

bool saveFbx(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err) {
    const Layout L = layout(s, o.naming);
    const size_t nb = L.bones.size();
    const float cm = 100.0f * o.scale;   // FBX's unit is the centimetre
    const size_t nf = std::max<size_t>(1, b.frames.size());
    const int64_t stop = (int64_t)((nf - 1) * (double)KTIME / b.fps);
    int64_t nextId = 1000000;
    auto id = [&]() { return nextId++; };
    std::vector<int64_t> modelId(nb), attrId(nb);
    for (size_t k = 0; k < nb; ++k) { modelId[k] = id(); attrId[k] = id(); }

    std::vector<Node> top;
    {   // the header
        Node h("FBXHeaderExtension");
        h.child("FBXHeaderVersion").I(1003);
        h.child("FBXVersion").I(7400);
        h.child("EncryptionType").I(0);
        Node& ts = h.child("CreationTimeStamp");
        const std::time_t now = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        ts.child("Version").I(1000); ts.child("Year").I(tm.tm_year + 1900); ts.child("Month").I(tm.tm_mon + 1); ts.child("Day").I(tm.tm_mday);
        ts.child("Hour").I(tm.tm_hour); ts.child("Minute").I(tm.tm_min); ts.child("Second").I(tm.tm_sec); ts.child("Millisecond").I(0);
        h.child("Creator").S("Pantomimus " PANTOMIMUS_VERSION);
        top.push_back(h);
        top.emplace_back("FileId"); top.back().R(std::string("\x28\xb3\x2a\xeb\xb6\x24\xcc\xc2\xbf\xc8\xb0\x2a\xa9\x2b\xfc\xf1", 16));
        top.emplace_back("CreationTime"); top.back().S("1970-01-01 10:00:00:000");
        top.emplace_back("Creator"); top.back().S("Pantomimus " PANTOMIMUS_VERSION);
    }
    {   // the world: Y up, -Z... front +Z, centimetres, the frame rate
        Node g("GlobalSettings");
        g.child("Version").I(1000);
        Node& p = g.child("Properties70");
        p.add(Pint("UpAxis", 1)).add(Pint("UpAxisSign", 1)).add(Pint("FrontAxis", 2)).add(Pint("FrontAxisSign", 1))
         .add(Pint("CoordAxis", 0)).add(Pint("CoordAxisSign", 1)).add(Pint("OriginalUpAxis", 1)).add(Pint("OriginalUpAxisSign", 1))
         .add(Pdouble("UnitScaleFactor", 1)).add(Pdouble("OriginalUnitScaleFactor", 1)).add(Pcolor("AmbientColor", V3()))
         .add(Pstr("DefaultCamera", "Producer Perspective"));
        const float fps = b.fps;
        int mode = 14;
        if (std::fabs(fps - 24) < 0.01f) mode = 11; else if (std::fabs(fps - 30) < 0.01f) mode = 6; else if (std::fabs(fps - 60) < 0.01f) mode = 3;
        else if (std::fabs(fps - 25) < 0.01f) mode = 10; else if (std::fabs(fps - 50) < 0.01f) mode = 4; else if (std::fabs(fps - 120) < 0.01f) mode = 1;
        p.add(Penum("TimeMode", mode)).add(Ptime("TimeSpanStart", 0)).add(Ptime("TimeSpanStop", stop)).add(Pdouble("CustomFrameRate", fps));
        top.push_back(g);
    }
    {
        Node d("Documents");
        d.child("Count").I(1);
        Node& doc = d.child("Document"); doc.L(id()).S("").S("Scene");
        Node& p = doc.child("Properties70");
        p.add([] { Node x = P("SourceObject", "object", "", ""); return x; }()).add(Pstr("ActiveAnimStackName", o.takeName));
        doc.child("RootNode").L(0);
        top.push_back(d);
        top.emplace_back("References");
    }
    Node objects("Objects"), conns("Connections");
    auto OO = [&](int64_t a, int64_t bb) { Node c("C"); c.S("OO").L(a).L(bb); conns.add(c); };
    auto OP = [&](int64_t a, int64_t bb, const std::string& prop) { Node c("C"); c.S("OP").L(a).L(bb).S(prop); conns.add(c); };
    int nModels = 0, nAttr = 0, nCurveNodes = 0, nCurves = 0, nGeom = 0, nDeformers = 0;
    // the bones
    for (size_t k = 0; k < nb; ++k) {
        const std::string& name = L.names[k];
        Node a("NodeAttribute"); a.L(attrId[k]).S(cls(name, "NodeAttribute")).S("LimbNode");
        a.child("Properties70").add(Pdouble("Size", 100 * o.scale));
        a.child("TypeFlags").S("Skeleton");
        objects.add(a); ++nAttr;
        Node m("Model"); m.L(modelId[k]).S(cls(name, "Model")).S("LimbNode");
        m.child("Version").I(232);
        Node& p = m.child("Properties70");
        p.add(Pvec("Lcl Translation", "Lcl Translation", restLocal(s, L, (int)k) * cm)).add(Pvec("Lcl Rotation", "Lcl Rotation", V3()))
         .add(Pvec("Lcl Scaling", "Lcl Scaling", V3(1, 1, 1))).add(Pint("DefaultAttributeIndex", 0)).add(Penum("InheritType", 1));
        m.child("Shading").C(true);
        m.child("Culling").S("CullingOff");
        objects.add(m); ++nModels;
        OO(attrId[k], modelId[k]);
        OO(modelId[k], L.parent[k] < 0 ? 0 : modelId[L.parent[k]]);
    }
    // the mannequin, skinned
    int64_t meshModel = 0;
    if (o.mannequin) {
        const Mesh mm = mannequin(s, cm);
        const int64_t geo = id(), skin = id();
        meshModel = id();
        // shared corners: each triangle written as a polygon (the last index negated, minus one)
        std::vector<double> verts(mm.pos.begin(), mm.pos.end());
        std::vector<int32_t> poly(mm.tri.size());
        for (size_t i = 0; i < mm.tri.size(); ++i) poly[i] = (i % 3 == 2) ? -(int32_t)mm.tri[i] - 1 : (int32_t)mm.tri[i];
        std::vector<double> nrm(mm.nrm.begin(), mm.nrm.end());   // by polygon vertex (each corner its own vertex here)
        Node g("Geometry"); g.L(geo).S(cls("Mannequin", "Geometry")).S("Mesh");
        g.child("Properties70");
        g.child("GeometryVersion").I(124);
        g.child("Vertices").arr('d', verts);
        g.child("PolygonVertexIndex").arr('i', poly);
        Node& ln = g.child("LayerElementNormal"); ln.I(0);
        ln.child("Version").I(101); ln.child("Name").S(""); ln.child("MappingInformationType").S("ByPolygonVertex");
        ln.child("ReferenceInformationType").S("Direct"); ln.child("Normals").arr('d', nrm);
        Node& lay = g.child("Layer"); lay.I(0);
        lay.child("Version").I(100);
        Node& le = lay.child("LayerElement"); le.child("Type").S("LayerElementNormal"); le.child("TypedIndex").I(0);
        objects.add(g); ++nGeom;
        Node mo("Model"); mo.L(meshModel).S(cls("Mannequin", "Model")).S("Mesh");
        mo.child("Version").I(232);
        mo.child("Properties70").add(Pvec("Lcl Translation", "Lcl Translation", V3())).add(Pvec("Lcl Rotation", "Lcl Rotation", V3()))
            .add(Pvec("Lcl Scaling", "Lcl Scaling", V3(1, 1, 1))).add(Pint("DefaultAttributeIndex", 0)).add(Penum("InheritType", 1));
        mo.child("Shading").C(true); mo.child("Culling").S("CullingOff");
        objects.add(mo); ++nModels;
        OO(meshModel, 0); OO(geo, meshModel);
        Node sk("Deformer"); sk.L(skin).S(cls("Mannequin", "Deformer")).S("Skin");
        sk.child("Version").I(101); sk.child("Link_DeformAcuracy").D(50);
        objects.add(sk); ++nDeformers;
        OO(skin, geo);
        std::vector<int> jointOf(s.bones.size(), -1);
        for (size_t k = 0; k < nb; ++k) jointOf[L.bones[k]] = (int)k;
        for (size_t i = 0; i < s.bones.size(); ++i)
            if (jointOf[i] < 0) { int p = s.bones[i].parent; while (p >= 0 && jointOf[p] < 0) p = s.bones[p].parent; jointOf[i] = p >= 0 ? jointOf[p] : 0; }
        std::vector<std::vector<int32_t>> idx(nb);
        for (size_t v = 0; v < mm.bone.size(); ++v) idx[jointOf[mm.bone[v]]].push_back((int32_t)v);
        for (size_t k = 0; k < nb; ++k) {
            const int64_t cl = id();
            const V3 h = s.bones[L.bones[k]].head * cm;
            std::vector<double> tr = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -h.x, -h.y, -h.z, 1};
            std::vector<double> tl = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, h.x, h.y, h.z, 1};
            std::vector<double> w(idx[k].size(), 1.0);
            Node c("Deformer"); c.L(cl).S(cls(L.names[k], "SubDeformer")).S("Cluster");
            c.child("Version").I(100);
            c.child("UserData").S("").S("");
            c.child("Indexes").arr('i', idx[k]);
            c.child("Weights").arr('d', w);
            c.child("Transform").arr('d', tr);
            c.child("TransformLink").arr('d', tl);
            objects.add(c); ++nDeformers;
            OO(cl, skin); OO(modelId[k], cl);
        }
        // the bind pose
        Node bp("Pose"); bp.L(id()).S(cls("BindPose", "Pose")).S("BindPose");
        bp.child("Type").S("BindPose"); bp.child("Version").I(100); bp.child("NbPoseNodes").I((int)nb + 1);
        { Node& pn = bp.child("PoseNode"); pn.child("Node").L(meshModel); pn.child("Matrix").arr('d', std::vector<double>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}); }
        for (size_t k = 0; k < nb; ++k) {
            const V3 h = s.bones[L.bones[k]].head * cm;
            Node& pn = bp.child("PoseNode"); pn.child("Node").L(modelId[k]);
            pn.child("Matrix").arr('d', std::vector<double>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, h.x, h.y, h.z, 1});
        }
        objects.add(bp);
    }
    // the animation
    const int64_t stack = id(), layer = id();
    {
        Node st("AnimationStack"); st.L(stack).S(cls(o.takeName, "AnimStack")).S("");
        st.child("Properties70").add(Ptime("LocalStart", 0)).add(Ptime("LocalStop", stop)).add(Ptime("ReferenceStart", 0)).add(Ptime("ReferenceStop", stop));
        objects.add(st);
        Node ly("AnimationLayer"); ly.L(layer).S(cls("BaseLayer", "AnimLayer")).S("");
        objects.add(ly);
        OO(layer, stack);
    }
    std::vector<int64_t> times(nf);
    for (size_t f = 0; f < nf; ++f) times[f] = (int64_t)(f * (double)KTIME / b.fps);
    std::vector<std::vector<V3>> eul(nb, std::vector<V3>(nf)), pos(nb, std::vector<V3>(nf));
    for (size_t f = 0; f < nf; ++f) {
        Skel::Pose p;
        if (b.frames.empty()) p.reset(s); else p = b.frames[f].pose;
        Skel::World w; Skel::forward(s, p, w);
        for (size_t k = 0; k < nb; ++k) {
            V3 t; Q r; localOf(s, L, w, (int)k, t, r);
            eul[k][f] = M::toEulerXYZ(r) * DEG;
            pos[k][f] = t * cm;
        }
    }
    for (auto& e : eul) unwrap(e);
    auto curves = [&](size_t k, const char* what, const char* prop, const std::vector<V3>& v) {
        const int64_t cn = id();
        Node n("AnimationCurveNode"); n.L(cn).S(cls(what, "AnimCurveNode")).S("");
        n.child("Properties70").add([&] { Node x = P("d|X", "Number", "", "A"); x.D(v[0].x); return x; }())
                               .add([&] { Node x = P("d|Y", "Number", "", "A"); x.D(v[0].y); return x; }())
                               .add([&] { Node x = P("d|Z", "Number", "", "A"); x.D(v[0].z); return x; }());
        objects.add(n); ++nCurveNodes;
        OO(cn, layer);
        OP(cn, modelId[k], prop);
        const char* axes[3] = {"d|X", "d|Y", "d|Z"};
        for (int c = 0; c < 3; ++c) {
            const int64_t cv = id();
            std::vector<float> vals(nf);
            for (size_t f = 0; f < nf; ++f) vals[f] = v[f][c];
            Node a("AnimationCurve"); a.L(cv).S(cls("", "AnimCurve")).S("");
            a.child("Default").D(vals[0]);
            a.child("KeyVer").I(4008);
            a.child("KeyTime").arr('l', times);
            a.child("KeyValueFloat").arr('f', vals);
            a.child("KeyAttrFlags").arr('i', std::vector<int32_t>{4 | (1 << 8) | (1 << 13)});   // linear
            a.child("KeyAttrDataFloat").arr('f', std::vector<float>{0, 0, 9.419963346924634e-30f, 0});
            a.child("KeyAttrRefCount").arr('i', std::vector<int32_t>{(int32_t)nf});
            objects.add(a); ++nCurves;
            OP(cv, cn, axes[c]);
        }
    };
    for (size_t k = 0; k < nb; ++k) {
        curves(k, "R", "Lcl Rotation", eul[k]);
        if (moves(s, L, (int)k)) curves(k, "T", "Lcl Translation", pos[k]);
    }
    {
        Node d("Definitions");
        d.child("Version").I(100);
        int total = 1;
        auto type = [&](const char* name, int count) { if (!count) return; Node& t = d.child("ObjectType"); t.S(name); t.child("Count").I(count); total += count; };
        { Node& t = d.child("ObjectType"); t.S("GlobalSettings"); t.child("Count").I(1); }
        type("Model", nModels); type("NodeAttribute", nAttr); type("Geometry", nGeom); type("Deformer", nDeformers);
        type("Pose", o.mannequin ? 1 : 0); type("AnimationStack", 1); type("AnimationLayer", 1);
        type("AnimationCurveNode", nCurveNodes); type("AnimationCurve", nCurves);
        d.kids.insert(d.kids.begin() + 1, Node("Count"));
        d.kids[1].I(total);
        top.push_back(d);
    }
    top.push_back(objects);
    top.push_back(conns);
    {
        Node t("Takes");
        t.child("Current").S(o.takeName);
        Node& tk = t.child("Take"); tk.S(o.takeName);
        tk.child("FileName").S(o.takeName + ".tak");
        tk.child("LocalTime").L(0).L(stop);
        tk.child("ReferenceTime").L(0).L(stop);
        top.push_back(t);
    }
    std::string out("Kaydara FBX Binary  \0\x1a\0", 23);
    const uint32_t version = 7400;
    out.append((const char*)&version, 4);
    for (const Node& n : top) write(out, n);
    out.append(13, '\0');
    out += std::string("\xfa\xbc\xab\x09\xd0\xc8\xd4\x66\xb1\x76\xfb\x83\x1c\xf7\x26\x7e", 16);
    out.append(4, '\0');
    size_t pad = ((out.size() + 15) & ~(size_t)15) - out.size();
    if (pad == 0) pad = 16;
    out.append(pad, '\0');
    out.append((const char*)&version, 4);
    out.append(120, '\0');
    out += std::string("\xf8\x5a\x8c\x6a\xde\xf5\xd9\x7e\xec\xe9\x0c\xe3\x75\x8f\x29\x0b", 16);
    std::ofstream f(fs::u8path(path), std::ios::binary);
    if (!f) { err = "Can't write " + path; return false; }
    f.write(out.data(), out.size());
    if (!f) { err = "Couldn't write all of " + path; return false; }
    return true;
}

bool save(const Skel::Skeleton& s, const Clip::Baked& b, const std::string& path, const Options& o, std::string& err) {
    const std::string e = Util::extension(path);
    if (e == ".bvh") return saveBvh(s, b, path, o, err);
    if (e == ".glb") return saveGlb(s, b, path, o, err);
    if (e == ".fbx") return saveFbx(s, b, path, o, err);
    err = "Name the file .bvh, .fbx or .glb";
    return false;
}

}  // namespace Export
