// Pantomimus - pictures (see image.h)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "image.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace Img {

bool load(const std::string& path, Image& out, std::string& err) {
#ifdef _WIN32
    FILE* f = _wfopen(fs::u8path(path).wstring().c_str(), L"rb");
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (!f) { err = "Can't open " + path; return false; }
    int w, h, c;
    unsigned char* d = stbi_load_from_file(f, &w, &h, &c, 3);
    fclose(f);
    if (!d) { err = "Can't read the picture " + path; return false; }
    out.w = w; out.h = h; out.rgb.assign(d, d + (size_t)w * h * 3);
    stbi_image_free(d);
    return true;
}

bool savePng(const std::string& path, const Image& im) {
    return stbi_write_png(path.c_str(), im.w, im.h, 3, im.rgb.data(), im.w * 3) != 0;
}

bool saveBmp(const std::string& path, int w, int h, const uint8_t* rgba) {
    return stbi_write_bmp(path.c_str(), w, h, 4, rgba) != 0;
}

static inline void sample(const Image& s, float x, float y, float* o) {
    // bilinear; outside: 0
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    float acc[3] = {0, 0, 0};
    const float wts[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    const int xs[4] = {x0, x0 + 1, x0, x0 + 1}, ys[4] = {y0, y0, y0 + 1, y0 + 1};
    for (int k = 0; k < 4; ++k) {
        if (xs[k] < 0 || ys[k] < 0 || xs[k] >= s.w || ys[k] >= s.h) continue;
        const uint8_t* p = &s.rgb[((size_t)ys[k] * s.w + xs[k]) * 3];
        acc[0] += p[0] * wts[k]; acc[1] += p[1] * wts[k]; acc[2] += p[2] * wts[k];
    }
    o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
}

void cropRotated(const Image& src, float cx, float cy, float sw, float sh, float rot, int n, float lo, float hi, float* out) {
    const float c = std::cos(rot), s = std::sin(rot), k = (hi - lo) / 255.0f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float u = ((i + 0.5f) / n - 0.5f) * sw, v = ((j + 0.5f) / n - 0.5f) * sh;
            const float x = cx + u * c - v * s - 0.5f, y = cy + u * s + v * c - 0.5f;
            float px[3];
            sample(src, x, y, px);
            float* o = out + ((size_t)j * n + i) * 3;
            o[0] = lo + px[0] * k; o[1] = lo + px[1] * k; o[2] = lo + px[2] * k;
        }
}

Letterbox letterbox(const Image& src, int n, float lo, float hi, float* out) {
    Letterbox L;
    const float side = (float)std::max(src.w, src.h);
    L.scale = n / side;
    L.padX = (side - src.w) * 0.5f * L.scale;
    L.padY = (side - src.h) * 0.5f * L.scale;
    const float k = (hi - lo) / 255.0f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = (i + 0.5f - L.padX) / L.scale - 0.5f, y = (j + 0.5f - L.padY) / L.scale - 0.5f;
            float px[3];
            sample(src, x, y, px);
            float* o = out + ((size_t)j * n + i) * 3;
            o[0] = lo + px[0] * k; o[1] = lo + px[1] * k; o[2] = lo + px[2] * k;
        }
    return L;
}

void mirror(Image& im) {
    for (int y = 0; y < im.h; ++y) {
        uint8_t* row = &im.rgb[(size_t)y * im.w * 3];
        for (int x = 0; x < im.w / 2; ++x)
            for (int c = 0; c < 3; ++c) std::swap(row[x * 3 + c], row[(im.w - 1 - x) * 3 + c]);
    }
}

Image downscale(const Image& im, int maxW) {
    if (im.w <= maxW) return im;
    const int f = (im.w + maxW - 1) / maxW;
    Image o; o.w = im.w / f; o.h = im.h / f; o.rgb.resize((size_t)o.w * o.h * 3);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x)
            for (int c = 0; c < 3; ++c) {
                int s = 0;
                for (int j = 0; j < f; ++j)
                    for (int i = 0; i < f; ++i) s += im.rgb[((size_t)(y * f + j) * im.w + x * f + i) * 3 + c];
                o.rgb[((size_t)y * o.w + x) * 3 + c] = (uint8_t)(s / (f * f));
            }
    return o;
}

}  // namespace Img
