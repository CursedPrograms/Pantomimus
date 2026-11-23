// Pantomimus - the 3D view (see view3d.h)
#include "view3d.h"
#include "export.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace View3D {

using M::V3;

// the GL 3.3 functions this view uses (loaded through SDL)
typedef char GLch;
#define GLFN(ret, name, args) typedef ret(APIENTRY* PF_##name) args; static PF_##name p_##name = nullptr;
GLFN(GLuint, glCreateShader, (GLenum))
GLFN(void, glShaderSource, (GLuint, GLsizei, const GLch* const*, const GLint*))
GLFN(void, glCompileShader, (GLuint))
GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint*))
GLFN(GLuint, glCreateProgram, ())
GLFN(void, glAttachShader, (GLuint, GLuint))
GLFN(void, glLinkProgram, (GLuint))
GLFN(void, glUseProgram, (GLuint))
GLFN(void, glDeleteShader, (GLuint))
GLFN(GLint, glGetUniformLocation, (GLuint, const GLch*))
GLFN(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))
GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))
GLFN(void, glUniform1f, (GLint, GLfloat))
GLFN(void, glGenVertexArrays, (GLsizei, GLuint*))
GLFN(void, glBindVertexArray, (GLuint))
GLFN(void, glGenBuffers, (GLsizei, GLuint*))
GLFN(void, glBindBuffer, (GLenum, GLuint))
GLFN(void, glBufferData, (GLenum, ptrdiff_t, const void*, GLenum))
GLFN(void, glEnableVertexAttribArray, (GLuint))
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLFN(void, glGenFramebuffers, (GLsizei, GLuint*))
GLFN(void, glBindFramebuffer, (GLenum, GLuint))
GLFN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLFN(void, glGenRenderbuffers, (GLsizei, GLuint*))
GLFN(void, glBindRenderbuffer, (GLenum, GLuint))
GLFN(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))
GLFN(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))

#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_VERTEX_SHADER_ 0x8B31
#define GL_ARRAY_BUFFER_ 0x8892
#define GL_STREAM_DRAW_ 0x88E0
#define GL_FRAMEBUFFER_ 0x8D40
#define GL_RENDERBUFFER_ 0x8D41
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_DEPTH_ATTACHMENT_ 0x8D00
#define GL_DEPTH_COMPONENT24_ 0x81A6

static bool ready = false;
static GLuint prog = 0, vao = 0, vbo = 0, fbo = 0, tex = 0, depth = 0;
static int fw = 0, fh = 0;
static GLint uMvp = -1, uLit = -1;

static bool load() {
#define L(n) p_##n = (PF_##n)SDL_GL_GetProcAddress(#n); if (!p_##n) return false;
    L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glGetShaderiv) L(glCreateProgram) L(glAttachShader) L(glLinkProgram)
    L(glUseProgram) L(glDeleteShader) L(glGetUniformLocation) L(glUniformMatrix4fv) L(glUniform3f) L(glUniform1f) L(glGenVertexArrays)
    L(glBindVertexArray) L(glGenBuffers) L(glBindBuffer) L(glBufferData) L(glEnableVertexAttribArray) L(glVertexAttribPointer)
    L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glGenRenderbuffers) L(glBindRenderbuffer)
    L(glRenderbufferStorage) L(glFramebufferRenderbuffer)
#undef L
    return true;
}

static GLuint shader(GLenum t, const char* src) {
    GLuint s = p_glCreateShader(t);
    p_glShaderSource(s, 1, &src, nullptr);
    p_glCompileShader(s);
    return s;
}

bool init() {
    if (ready) return true;
    if (!load()) return false;
    // position, normal, colour; lit: 1 shaded, 0 flat (lines and points)
    const char* vs = "#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec3 n; layout(location=2) in vec3 c;\n"
                     "uniform mat4 mvp; out vec3 vn; out vec3 vc; void main(){ gl_Position = mvp*vec4(p,1); vn=n; vc=c; }";
    const char* fs = "#version 330 core\nin vec3 vn; in vec3 vc; uniform float lit; out vec4 o;\n"
                     "void main(){ float d = max(dot(normalize(vn), normalize(vec3(0.4,0.8,0.6))),0.0)*0.65+0.35; o = vec4(vc*mix(1.0,d,lit),1); }";
    prog = p_glCreateProgram();
    const GLuint a = shader(GL_VERTEX_SHADER_, vs), b = shader(GL_FRAGMENT_SHADER_, fs);
    p_glAttachShader(prog, a); p_glAttachShader(prog, b); p_glLinkProgram(prog);
    p_glDeleteShader(a); p_glDeleteShader(b);
    uMvp = p_glGetUniformLocation(prog, "mvp"); uLit = p_glGetUniformLocation(prog, "lit");
    p_glGenVertexArrays(1, &vao); p_glGenBuffers(1, &vbo);
    p_glBindVertexArray(vao); p_glBindBuffer(GL_ARRAY_BUFFER_, vbo);
    for (int i = 0; i < 3; ++i) { p_glEnableVertexAttribArray(i); p_glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 36, (const void*)(size_t)(i * 12)); }
    p_glBindVertexArray(0);
    ready = true;
    return true;
}

void shutdown() { ready = false; }

static void target(int w, int h) {
    if (fbo && w == fw && h == fh) return;
    if (!fbo) { p_glGenFramebuffers(1, &fbo); glGenTextures(1, &tex); p_glGenRenderbuffers(1, &depth); }
    fw = w; fh = h;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glBindRenderbuffer(GL_RENDERBUFFER_, depth);
    p_glRenderbufferStorage(GL_RENDERBUFFER_, GL_DEPTH_COMPONENT24_, w, h);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, tex, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_ATTACHMENT_, GL_RENDERBUFFER_, depth);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
}

static void vert(std::vector<float>& v, const V3& p, const V3& n, const V3& c) { v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, c.x, c.y, c.z}); }

static void draw(const std::vector<float>& v, GLenum mode, float lit) {
    if (v.empty()) return;
    p_glUniform1f(uLit, lit);
    p_glBindBuffer(GL_ARRAY_BUFFER_, vbo);
    p_glBufferData(GL_ARRAY_BUFFER_, (ptrdiff_t)(v.size() * 4), v.data(), GL_STREAM_DRAW_);
    glDrawArrays(mode, 0, (GLsizei)(v.size() / 9));
}

// side colours (content, not controls): the body's left warm, its right cool, the middle grey
static V3 colourOf(const Skel::Skeleton& s, int b) {
    const std::string& n = s.bones[b].name;
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "_l") == 0) return {0.86f, 0.62f, 0.36f};
    if (n.size() > 2 && n.compare(n.size() - 2, 2, "_r") == 0) return {0.36f, 0.70f, 0.66f};
    return {0.72f, 0.72f, 0.72f};
}

unsigned render(int w, int h, const Camera& cam, const Look& look, const Skel::Skeleton& s, const Skel::Pose* pose, const bool contact[2],
                const Pose::Result* lm, bool mirror) {
    if (!ready || w < 8 || h < 8) return 0;
    target(w, h);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0x14 / 255.0f, 0x14 / 255.0f, 0x14 / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    Skel::Pose rest; rest.reset(s);
    const Skel::Pose& p = pose && pose->local.size() == s.bones.size() ? *pose : rest;
    Skel::World wd; Skel::forward(s, p, wd);
    V3 tgt = cam.target;
    if (look.follow && s.hips >= 0) { tgt.x += wd.head[s.hips].x; tgt.z += wd.head[s.hips].z; }
    const V3 eye = tgt + V3(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), std::cos(cam.yaw) * std::cos(cam.pitch)) * cam.dist;
    const M::M4 mvp = M::perspective(0.8f, (float)w / h, 0.05f, 100.0f) * M::lookAt(eye, tgt, V3(0, 1, 0));
    p_glUseProgram(prog);
    p_glUniformMatrix4fv(uMvp, 1, GL_FALSE, mvp.m);
    p_glBindVertexArray(vao);
    std::vector<float> v;
    if (look.floor) {   // a grid round the body, a line every half metre
        const float cx = std::round(tgt.x * 2) / 2, cz = std::round(tgt.z * 2) / 2;
        for (int i = -10; i <= 10; ++i) {
            const V3 c = i == 0 ? V3(0.32f, 0.32f, 0.32f) : V3(0.22f, 0.22f, 0.22f);
            vert(v, {cx + i * 0.5f, 0, cz - 5}, {0, 1, 0}, c); vert(v, {cx + i * 0.5f, 0, cz + 5}, {0, 1, 0}, c);
            vert(v, {cx - 5, 0, cz + i * 0.5f}, {0, 1, 0}, c); vert(v, {cx + 5, 0, cz + i * 0.5f}, {0, 1, 0}, c);
        }
        draw(v, GL_LINES, 0);
        v.clear();
    }
    if (look.mannequin) {
        static Export::Mesh mesh;
        static const Skel::Skeleton* meshFor = nullptr;
        static size_t meshBones = 0; static float meshH = 0;
        if (meshFor != &s || meshBones != s.bones.size() || meshH != s.height()) { mesh = Export::mannequin(s, 1); meshFor = &s; meshBones = s.bones.size(); meshH = s.height(); }
        const Skel::Roles ro = Skel::roles(s);
        for (size_t i = 0; i < mesh.bone.size(); ++i) {
            const int b = mesh.bone[i];
            const V3 rp(mesh.pos[i * 3], mesh.pos[i * 3 + 1], mesh.pos[i * 3 + 2]), rn(mesh.nrm[i * 3], mesh.nrm[i * 3 + 1], mesh.nrm[i * 3 + 2]);
            const V3 wp = wd.head[b] + M::rotate(wd.rot[b], rp - s.bones[b].head), wn = M::rotate(wd.rot[b], rn);
            V3 c = colourOf(s, b);
            for (int side = 0; side < 2; ++side)
                if (contact && contact[side] && (b == ro.foot[side] || b == ro.ball[side])) c = V3(0.35f, 0.69f, 0.29f);   // planted
            vert(v, wp, wn, c);
        }
        draw(v, GL_TRIANGLES, 1);
        v.clear();
    }
    if (look.bones) {   // the bones as lines over everything
        glDisable(GL_DEPTH_TEST);
        for (size_t i = 0; i < s.bones.size(); ++i) {
            const V3 c = V3(0.9f, 0.9f, 0.9f);
            vert(v, wd.head[i], {0, 1, 0}, c); vert(v, wd.tail[i], {0, 1, 0}, c);
        }
        draw(v, GL_LINES, 0);
        v.clear();
        glEnable(GL_DEPTH_TEST);
    }
    if (look.landmarks && lm && lm->found && s.hips >= 0) {   // BlazePose's world landmarks, round the hips
        glPointSize(5);
        const V3 at = wd.head[s.hips];
        for (int i = 0; i < Pose::N; ++i) {
            const Pose::Point& q = lm->world[i];
            vert(v, at + V3(mirror ? -q.x : q.x, -q.y, -q.z), {0, 1, 0}, lm->visibility[i] > 0.5f ? V3(0.9f, 0.9f, 0.9f) : V3(0.5f, 0.5f, 0.5f));
        }
        glDisable(GL_DEPTH_TEST);
        draw(v, GL_POINTS, 0);
        glEnable(GL_DEPTH_TEST);
    }
    p_glBindVertexArray(0);
    p_glUseProgram(0);
    p_glBindFramebuffer(GL_FRAMEBUFFER_, 0);
    glDisable(GL_DEPTH_TEST);
    return tex;
}

void orbit(Camera& c, float dx, float dy) {
    c.yaw -= dx * 0.01f;
    c.pitch = std::clamp(c.pitch + dy * 0.01f, -1.5f, 1.5f);
}
void pan(Camera& c, float dx, float dy, int h) {
    const float k = c.dist * 0.8f / std::max(1, h);
    const V3 right(std::cos(c.yaw), 0, -std::sin(c.yaw));
    c.target += right * (-dx * k);
    c.target.y += dy * k;
}
void zoom(Camera& c, float steps) { c.dist = std::clamp(c.dist * std::pow(0.88f, steps), 0.4f, 30.0f); }

}  // namespace View3D
