// Pantomimus - the splash: a mime caught in three moments of one movement (the grey ones the frames before, the blue one
// now), its landmarks as dots and its bones as lines, the name, the version, the credits and what's loading.
// Atelier greys and blue, solid. (The app's logo is a placeholder until the painted one comes.)
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>
#include <initializer_list>

#ifndef PANTOMIMUS_VERSION
#define PANTOMIMUS_VERSION "1.0.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;
void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f ? f : ImGui::GetFont(), size, p, col, s); }
static float textWidth(ImFont* f, float size, const char* s) { return (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0, s).x; }

// a figure as tracked: joints as dots, bones as lines; t 0..1 through a wave of the arm and a step
static void figure(ImDrawList* dl, ImVec2 o, float s, ImU32 col, float t, float thick) {
    auto P = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
    const float a = -0.4f + t * 1.9f, step = t * 14;
    const ImVec2 head = P(t * 3, -88), neck = P(t * 2, -70), hip = P(0, -12);
    const ImVec2 sl = P(-18 + t * 2, -64), sr = P(18 + t * 2, -64);
    const ImVec2 el = P(-30, -40), wl = P(-30 + std::sin(t * 2.2f) * 6, -16);
    const ImVec2 er = P(18 + 26 * std::cos(a), -64 - 26 * std::sin(a)), wr = P(18 + 26 * std::cos(a) + 24 * std::cos(a + 0.7f), -64 - 26 * std::sin(a) - 24 * std::sin(a + 0.7f));
    const ImVec2 hl = P(-10, -12), hr = P(10, -12), kl = P(-12 - step * 0.4f, 16), kr = P(12 + step, 14 - t * 4), al = P(-13 - step * 0.6f, 44), ar = P(14 + step * 1.6f, 42 - t * 6);
    const ImVec2 lines[][2] = {{neck, hip}, {sl, sr}, {sl, el}, {el, wl}, {sr, er}, {er, wr}, {hl, hr}, {hl, kl}, {kl, al}, {hr, kr}, {kr, ar}};
    for (const auto& l : lines) dl->AddLine(l[0], l[1], col, thick);
    dl->AddCircle(head, 10 * s, col, 24, thick);
    for (ImVec2 j : {sl, sr, el, wl, er, wr, hl, hr, kl, kr, al, ar}) dl->AddCircleFilled(j, thick + 1.5f, col, 10);
}

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);
    // the logo, drawn large: the movement in three frames
    const float px = x + 440, py = y + 205;
    dl->AddLine(ImVec2(px - 120, py + 64), ImVec2(px + 150, py + 64), LINE, 2);
    figure(dl, ImVec2(px - 50, py), 1.45f, IM_COL32(0x3c, 0x3c, 0x3c, 255), 0.0f, 3);
    figure(dl, ImVec2(px - 10, py), 1.45f, IM_COL32(0x5a, 0x5a, 0x5a, 255), 0.45f, 3);
    figure(dl, ImVec2(px + 32, py), 1.45f, BLUE, 1.0f, 4);
    // the name in two tones
    const float ts = 46, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Panto");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Panto"), ty), BLUE, "mimus");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 60), DIM, "Version " PANTOMIMUS_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 82), DIM, "Motion capture from a webcam");
    const float cs = 13, cry = y + 289;
    text(dl, smallFont, cs, ImVec2(tx, cry), DIM, "BlazePose and Hands: MediaPipe (Google, Apache-2.0).");
    text(dl, smallFont, cs, ImVec2(tx, cry + 17), DIM, "The skeleton: measured on MakeHuman's body (CC0).");
    text(dl, smallFont, cs, ImVec2(tx, cry + 34), DIM, "ONNX Runtime, Dear ImGui, SDL2, stb.");
    text(dl, smallFont, cs, ImVec2(tx, cry + 51), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");
    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), BLUE);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}  // namespace Splash
