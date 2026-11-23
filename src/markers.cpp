// Pantomimus - Sharpie markers (see markers.h)
#include "markers.h"

#include <algorithm>
#include <cmath>

namespace Markers {

// where to draw the dots: on skin, away from the eyes and nostrils, where the face moves (and a few that don't, the
// forehead's and the nose's, which show how the head itself moves)
const Spot LAYOUT[COUNT] = {
    {"forehead (middle)", 151, 0, 0}, {"forehead (right)", 108, 0, 0}, {"forehead (left)", 337, 0, 0},
    // just above the brows, on skin (they move with the brows)
    {"brow inner (right)", 107, 0, -0.04f}, {"brow middle (right)", 105, 0, -0.05f}, {"brow outer (right)", 70, 0.05f, -0.08f},
    {"brow inner (left)", 336, 0, -0.04f}, {"brow middle (left)", 334, 0, -0.05f}, {"brow outer (left)", 300, -0.05f, -0.08f},
    {"cheekbone (right)", 50, 0, 0}, {"cheek (right)", 205, 0, 0}, {"cheekbone (left)", 280, 0, 0}, {"cheek (left)", 425, 0, 0},
    {"nose tip", 1, 0, 0},
    // just outside the mouth's corners, above the upper lip and below the lower one, spread along them
    {"mouth corner (right)", 61, -0.09f, 0}, {"mouth corner (left)", 291, 0.09f, 0},
    {"upper lip (middle)", 0, 0, -0.03f}, {"upper lip (right)", 40, 0, -0.06f}, {"upper lip (left)", 270, 0, -0.06f},
    {"lower lip (middle)", 17, 0, 0.03f}, {"lower lip (right)", 181, 0, 0.03f}, {"lower lip (left)", 405, 0, 0.03f},
    {"chin", 152, 0, -0.08f},
    {"jaw (right)", 172, 0.05f, 0}, {"jaw (left)", 397, -0.05f, 0}};

void spotPosition(const Face::Result& f, int i, int w, int h, float& px, float& py) {
    const FaceFrame F = faceFrame(f, w, h);
    float lx, ly;
    F.toFace(f.image[LAYOUT[i].landmark].x * w, f.image[LAYOUT[i].landmark].y * h, lx, ly);
    F.toFrame(lx + LAYOUT[i].dx, ly + LAYOUT[i].dy, px, py);
}

void FaceFrame::toFace(float px, float py, float& fx, float& fy) const {
    const float dx = px - ox, dy = py - oy;
    fx = (dx * ax + dy * ay) / scale;
    fy = (-dx * ay + dy * ax) / scale;
}
void FaceFrame::toFrame(float fx, float fy, float& px, float& py) const {
    px = ox + (fx * ax - fy * ay) * scale;
    py = oy + (fx * ay + fy * ax) * scale;
}

FaceFrame faceFrame(const Face::Result& f, int w, int h) {
    FaceFrame F;
    const float rx = f.image[Face::R_EYE_OUT].x * w, ry = f.image[Face::R_EYE_OUT].y * h;
    const float lx = f.image[Face::L_EYE_OUT].x * w, ly = f.image[Face::L_EYE_OUT].y * h;
    F.ox = (rx + lx) / 2; F.oy = (ry + ly) / 2;
    const float dx = lx - rx, dy = ly - ry, d = std::hypot(dx, dy);
    F.scale = std::max(d, 1.0f);
    F.ax = d > 0 ? dx / d : 1; F.ay = d > 0 ? dy / d : 0;
    return F;
}

// is (x, y) inside the polygon of these landmarks (frame pixels)
static bool inside(const Face::Result& f, const int* idx, int n, int w, int h, float x, float y, float grow) {
    float cx = 0, cy = 0;
    for (int i = 0; i < n; ++i) { cx += f.image[idx[i]].x * w; cy += f.image[idx[i]].y * h; }
    cx /= n; cy /= n;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = cx + (f.image[idx[i]].x * w - cx) * grow, yi = cy + (f.image[idx[i]].y * h - cy) * grow;
        const float xj = cx + (f.image[idx[j]].x * w - cx) * grow, yj = cy + (f.image[idx[j]].y * h - cy) * grow;
        if (((yi > y) != (yj > y)) && x < (xj - xi) * (y - yi) / (yj - yi + 1e-9f) + xi) in = !in;
    }
    return in;
}
static const int EYE_R[] = {33, 246, 161, 160, 159, 158, 157, 173, 133, 155, 154, 153, 145, 144, 163, 7};
static const int EYE_L[] = {263, 466, 388, 387, 386, 385, 384, 398, 362, 382, 381, 380, 374, 373, 390, 249};
static const int LIPS_IN[] = {78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95};

std::vector<Blob> findDots(const Image& im, const Face::Result& f, const Settings& s) {
    std::vector<Blob> out;
    if (!f.found || im.empty()) return out;
    const int W = im.w, H = im.h;
    const FaceFrame F = faceFrame(f, W, H);
    const float faceW = F.scale * 2.2f;                       // the eyes' corners are ~45% of the face's width
    const float rMin = std::max(1.0f, s.minDot * faceW), rMax = std::max(rMin + 1, s.maxDot * faceW);
    // the face's box, a little larger
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 468; ++i) { x0 = std::min(x0, f.image[i].x * W); x1 = std::max(x1, f.image[i].x * W); y0 = std::min(y0, f.image[i].y * H); y1 = std::max(y1, f.image[i].y * H); }
    const float pad = rMax * 2;
    const int bx0 = std::max(0, (int)(x0 - pad)), by0 = std::max(0, (int)(y0 - pad)), bx1 = std::min(W - 1, (int)(x1 + pad)), by1 = std::min(H - 1, (int)(y1 + pad));
    const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
    if (bw < 8 || bh < 8) return out;
    // brightness, and its local mean over a window a few dots wide (an integral image)
    std::vector<float> g((size_t)bw * bh);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const uint8_t* p = &im.rgb[((size_t)(by0 + y) * W + bx0 + x) * 3];
            g[(size_t)y * bw + x] = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
        }
    std::vector<double> I((size_t)(bw + 1) * (bh + 1), 0.0);
    for (int y = 0; y < bh; ++y) {
        double row = 0;
        for (int x = 0; x < bw; ++x) { row += g[(size_t)y * bw + x]; I[(size_t)(y + 1) * (bw + 1) + x + 1] = I[(size_t)y * (bw + 1) + x + 1] + row; }
    }
    const int win = std::max(3, (int)(rMax * 3));
    auto mean = [&](int x, int y) {
        const int a0 = std::max(0, x - win), a1 = std::min(bw, x + win + 1), b0 = std::max(0, y - win), b1 = std::min(bh, y + win + 1);
        const double sum = I[(size_t)b1 * (bw + 1) + a1] - I[(size_t)b0 * (bw + 1) + a1] - I[(size_t)b1 * (bw + 1) + a0] + I[(size_t)b0 * (bw + 1) + a0];
        return (float)(sum / ((a1 - a0) * (b1 - b0)));
    };
    std::vector<float> dark((size_t)bw * bh, 0);
    std::vector<uint8_t> mask((size_t)bw * bh, 0);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const float m = mean(x, y), v = g[(size_t)y * bw + x];
            const float d = m > 1 ? (m - v) / m : 0;
            dark[(size_t)y * bw + x] = d;
            if (d >= s.darkness) mask[(size_t)y * bw + x] = 1;
        }
    // the dark patches, each one a candidate
    std::vector<int> stack;
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            if (mask[(size_t)y * bw + x] != 1) continue;
            stack.assign(1, y * bw + x);
            mask[(size_t)y * bw + x] = 2;
            double sw = 0, sx = 0, sy = 0; int area = 0, mx0 = x, mx1 = x, my0 = y, my1 = y;
            float peak = 0;
            while (!stack.empty()) {
                const int k = stack.back(); stack.pop_back();
                const int cx = k % bw, cy = k / bw;
                const float d = dark[(size_t)k];
                sw += d; sx += d * cx; sy += d * cy; ++area; peak = std::max(peak, d);
                mx0 = std::min(mx0, cx); mx1 = std::max(mx1, cx); my0 = std::min(my0, cy); my1 = std::max(my1, cy);
                if (area > (int)(3.2f * rMax * rMax * 4)) continue;   // far too big: let it go
                const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& o : nb) {
                    const int nx = cx + o[0], ny = cy + o[1];
                    if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue;
                    uint8_t& m = mask[(size_t)ny * bw + nx];
                    if (m == 1) { m = 2; stack.push_back(ny * bw + nx); }
                }
            }
            const float bwid = (float)(mx1 - mx0 + 1), bhgt = (float)(my1 - my0 + 1);
            const float r = std::sqrt(area / M::PI);
            if (r < rMin || r > rMax || sw <= 0) continue;
            if (std::max(bwid, bhgt) > 2.2f * std::min(bwid, bhgt)) continue;           // long: a brow, a lash, a crease
            if (area < s.roundness * bwid * bhgt) continue;                              // ragged: hair, stubble
            Blob b;
            b.x = bx0 + (float)(sx / sw) + 0.5f; b.y = by0 + (float)(sy / sw) + 0.5f; b.r = r; b.dark = peak;
            // an ink dot is lighter all the way round it; a crease, a shadow's edge or a hairline is dark on a side
            {
                const float lx = b.x - bx0 - 0.5f, ly = b.y - by0 - 0.5f;
                auto at = [&](float x, float y) {
                    const int ix = std::clamp((int)std::lround(x), 0, bw - 1), iy = std::clamp((int)std::lround(y), 0, bh - 1);
                    return g[(size_t)iy * bw + ix];
                };
                const float core = at(lx, ly);
                const float rr = std::max(r * 2.2f, r + 3.0f);
                float ring[16], sorted[16];
                for (int k = 0; k < 16; ++k) {
                    const float a = k * 2 * M::PI / 16;
                    ring[k] = sorted[k] = at(lx + std::cos(a) * rr, ly + std::sin(a) * rr);
                }
                std::sort(sorted, sorted + 16);
                const float skin = (sorted[7] + sorted[8]) / 2;                       // the ring's median: the skin
                if (skin - core < s.darkness * skin) continue;                        // not dark enough against it
                // a neighbouring dot may cross the ring in a place or two; a shadow's edge darkens half of it
                int darkSamples = 0;
                for (float v : ring) if (v - core < 0.5f * (skin - core)) ++darkSamples;
                if (darkSamples > 4) continue;
            }
            // not the eyes, the inside of the mouth or the nostrils (each between its wing, 98 / 327, and the nose's base, 2)
            if (inside(f, EYE_R, 16, W, H, b.x, b.y, 1.25f) || inside(f, EYE_L, 16, W, H, b.x, b.y, 1.25f) ||
                inside(f, LIPS_IN, 20, W, H, b.x, b.y, 1.0f)) continue;
            bool nostril = false;
            for (int wing : {98, 327}) {
                const float nx = (f.image[wing].x + f.image[2].x) / 2 * W, ny = (f.image[wing].y + f.image[2].y) / 2 * H;
                if (std::hypot(b.x - nx, b.y - ny) < 0.07f * F.scale) nostril = true;
            }
            if (nostril) continue;
            out.push_back(b);
        }
    return out;
}

int Tracker::calibrate(const Image& frame, const Face::Result& f, const Settings& s) {
    clear();
    if (!f.found) return 0;
    const int W = frame.w, H = frame.h;
    const FaceFrame F = faceFrame(f, W, H);
    const std::vector<Blob> dots = findDots(frame, f, s);
    std::vector<bool> used(dots.size(), false);
    int n = 0;
    // each spot takes the nearest unused dot within reach of its landmark (a little further than when tracking)
    const float reach = s.search * 2.2f * F.scale * 1.5f;
    for (int i = 0; i < COUNT; ++i) {
        const float lx = f.image[LAYOUT[i].landmark].x * W, ly = f.image[LAYOUT[i].landmark].y * H;
        float sx, sy;
        spotPosition(f, i, W, H, sx, sy);   // where it was meant to be drawn
        int best = -1; float bd = reach;
        for (size_t k = 0; k < dots.size(); ++k) {
            if (used[k]) continue;
            const float d = std::hypot(dots[k].x - sx, dots[k].y - sy);
            if (d < bd) { bd = d; best = (int)k; }
        }
        if (best < 0) continue;
        used[(size_t)best] = true;
        float dfx, dfy, lfx, lfy;
        F.toFace(dots[(size_t)best].x, dots[(size_t)best].y, dfx, dfy);
        F.toFace(lx, ly, lfx, lfy);
        offX_[i] = dfx - lfx; offY_[i] = dfy - lfy;
        have_[i] = true;
        ++n;
    }
    calibrated_ = n > 0;
    return n;
}

void Tracker::setCalibration(const bool have[COUNT], const float ox[COUNT], const float oy[COUNT]) {
    calibrated_ = false;
    for (int i = 0; i < COUNT; ++i) { have_[i] = have[i]; offX_[i] = ox[i]; offY_[i] = oy[i]; calibrated_ = calibrated_ || have[i]; }
}

void Tracker::track(const Image& frame, Face::Result& f, const Settings& s, Result& out) {
    out = Result();
    out.calibrated = calibrated_;
    if (!f.found) return;
    const int W = frame.w, H = frame.h;
    const FaceFrame F = faceFrame(f, W, H);
    out.blobs = findDots(frame, f, s);
    if (!calibrated_) return;
    // where the mesh puts each dot, then the nearest dots to those, closest pairs first
    float px[COUNT], py[COUNT];
    struct Pair { float d; int spot, dot; };
    std::vector<Pair> pairs;
    const float reach = s.search * 2.2f * F.scale;
    for (int i = 0; i < COUNT; ++i) {
        if (!have_[i]) continue;
        float lfx, lfy;
        F.toFace(f.image[LAYOUT[i].landmark].x * W, f.image[LAYOUT[i].landmark].y * H, lfx, lfy);
        F.toFrame(lfx + offX_[i], lfy + offY_[i], px[i], py[i]);
        for (size_t k = 0; k < out.blobs.size(); ++k) {
            const float d = std::hypot(out.blobs[k].x - px[i], out.blobs[k].y - py[i]);
            if (d <= reach) pairs.push_back({d, i, (int)k});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    std::vector<bool> dotUsed(out.blobs.size(), false);
    for (const Pair& p : pairs) {
        if (out.seen[p.spot] || dotUsed[(size_t)p.dot]) continue;
        out.seen[p.spot] = true; dotUsed[(size_t)p.dot] = true;
        const Blob& b = out.blobs[(size_t)p.dot];
        out.x[p.spot] = b.x / W; out.y[p.spot] = b.y / H;
        F.toFace(b.x, b.y, out.fx[p.spot], out.fy[p.spot]);
        ++out.found;
        if (s.blend > 0) {
            // the landmark where the dot says it is (the dot minus its offset)
            float tx, ty;
            F.toFrame(out.fx[p.spot] - offX_[p.spot], out.fy[p.spot] - offY_[p.spot], tx, ty);
            Pose::Point& l = f.image[LAYOUT[p.spot].landmark];
            const float k = std::clamp(s.blend, 0.0f, 1.0f);
            l.x += (tx / W - l.x) * k;
            l.y += (ty / H - l.y) * k;
        }
    }
}

}  // namespace Markers
