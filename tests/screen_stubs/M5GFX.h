#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// Match M5GFX's RGB565 constants and its distinction between unsigned 32-bit
// RGB888 arguments and other integer RGB565 arguments.
constexpr int TFT_BLACK = 0x0000, TFT_WHITE = 0xFFFF, TFT_LIGHTGREY = 0xD69A;
constexpr int TFT_GREEN = 0x07E0, TFT_CYAN = 0x07FF;
namespace lgfx { inline namespace v1 {
namespace fonts { struct Font {}; static const Font Font0{}; }
class LGFX_Device {
 public:
  struct Operation {
    bool isText;
    int32_t x, y, width, height;
    std::string text;
    uint32_t color;
    uint8_t scale;
  };
  LGFX_Device(int32_t width, int32_t height)
      : pixels(width * height), width_(width), height_(height) {}
  int32_t width() { ++dimensionReads; return width_; }
  int32_t height() { ++dimensionReads; return height_; }
  void setFont(const fonts::Font* font) { assert(font != nullptr); }
  void setTextPadding(uint32_t padding) { assert(padding == 0); }
  void setTextSize(uint8_t scale) { scale_ = scale; }
  template<class F, class B> void setTextColor(F foreground, B background) {
    foreground_ = rgb(foreground); background_ = rgb(background);
  }
  template<class T> void fillScreen(T color) {
    ++fullClears;
    fillRect(0, 0, width_, height_, color);
  }
  template<class T> void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, T color) {
    operations.push_back({false, x, y, w, h, "", rgb(color), 1});
    paint(x, y, w, h, rgb(color));
  }
  void drawString(const char* value, int32_t x, int32_t y) {
    const int32_t w = std::strlen(value) * 6 * scale_, h = 8 * scale_;
    operations.push_back({true, x, y, w, h, value, foreground_, scale_});
    paint(x, y, w, h, background_);
    // A solid glyph box is enough to detect stale text after partial clearing.
    paint(x, y, w, h, foreground_);
  }
  void drawRightString(const char* value, int32_t x, int32_t y) {
    drawString(value, x - std::strlen(value) * 6 * scale_, y);
  }
  std::vector<Operation> operations;
  std::vector<uint32_t> pixels;
  unsigned fullClears = 0, dimensionReads = 0;
 private:
  template<class T> static uint32_t rgb(T color) {
    if (std::is_same<T, uint32_t>::value) return uint32_t(color) & 0xFFFFFF;
    const unsigned r = (color >> 11) & 31, g = (color >> 5) & 63, b = color & 31;
    return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
  }
  void paint(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0);
    assert(x + w <= width_ && y + h <= height_);
    for (int32_t row = y; row < y + h; ++row)
      for (int32_t col = x; col < x + w; ++col) pixels[row * width_ + col] = color;
  }
  int32_t width_, height_;
  uint8_t scale_ = 1;
  uint32_t foreground_ = 0xFFFFFF, background_ = 0;
};
} }
