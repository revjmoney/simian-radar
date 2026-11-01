#pragma once
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>

// Drawing surface for the screens: the Adafruit SSD1306 driver (same init as MeshCore on this
// board) with U8g2 fonts on top. Coordinates are portrait, 64 x 128.
class Canvas {
 public:
  explicit Canvas(Adafruit_SSD1306& d) : d_(d) {}
  void begin() { fonts_.begin(d_); fonts_.setFontMode(1); }
  void clear() { d_.clearDisplay(); color_ = SSD1306_WHITE; }
  void send() { d_.display(); }

  void setFont(const uint8_t* f) { fonts_.setFont(f); }
  void setDrawColor(int c) { color_ = c ? SSD1306_WHITE : SSD1306_BLACK; }
  void drawStr(int x, int y, const char* s) {
    fonts_.setForegroundColor(color_);
    fonts_.drawStr(x, y, s);
  }
  int getStrWidth(const char* s) { return fonts_.getUTF8Width(s); }

  void drawBox(int x, int y, int w, int h) { d_.fillRect(x, y, w, h, color_); }
  void drawFrame(int x, int y, int w, int h) { d_.drawRect(x, y, w, h, color_); }
  void drawHLine(int x, int y, int w) { d_.drawFastHLine(x, y, w, color_); }
  void drawLine(int x0, int y0, int x1, int y1) { d_.drawLine(x0, y0, x1, y1, color_); }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2) { d_.fillTriangle(x0, y0, x1, y1, x2, y2, color_); }
  void drawCircle(int x, int y, int r) { d_.drawCircle(x, y, r, color_); }
  void drawUpperArc(int x, int y, int r) { d_.drawCircleHelper(x, y, r, 0x1 | 0x2, color_); }
  void drawDisc(int x, int y, int r) { d_.fillCircle(x, y, r, color_); }

 private:
  Adafruit_SSD1306& d_;
  U8G2_FOR_ADAFRUIT_GFX fonts_;
  uint16_t color_ = SSD1306_WHITE;
};
