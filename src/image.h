// Pantomimus - pictures: RGB8 frames, loading and saving, and the warps the models need (a rotated square cut out of
// the frame and scaled to the model's input, as floats).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;              // RGB, 3 bytes a pixel, rows top to bottom
    std::vector<uint8_t> rgb;
    bool empty() const { return w <= 0 || h <= 0 || rgb.empty(); }
};

namespace Img {
bool load(const std::string& path, Image& out, std::string& err);
bool savePng(const std::string& path, const Image& im);
bool saveBmp(const std::string& path, int w, int h, const uint8_t* rgba);   // RGBA, top row first

// a rotated rectangle of the frame (centre cx, cy, size sw x sh in pixels, turned by rot radians) sampled into an
// n x n x 3 float tensor; lo..hi: the range the 0..255 values map to; outside the frame: black (lo)
void cropRotated(const Image& src, float cx, float cy, float sw, float sh, float rot, int n, float lo, float hi, float* out);
// the whole frame letterboxed into an n x n tensor (black bars); pad tells where the picture sits (for mapping back)
struct Letterbox { float scale = 1, padX = 0, padY = 0; };   // tensor px = frame px * scale + pad
Letterbox letterbox(const Image& src, int n, float lo, float hi, float* out);
// the frame mirrored left to right
void mirror(Image& im);
// smaller (for previews and slow machines): every factor-th pixel, box filtered
Image downscale(const Image& im, int maxW);
}
