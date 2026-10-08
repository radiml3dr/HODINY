#pragma once
#include <Arduino.h>

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

namespace color {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t SEG_OFF = rgb(22, 26, 30);
constexpr uint16_t CLOCK = rgb(120, 220, 255);
constexpr uint16_t STOPWATCH = rgb(120, 255, 140);
constexpr uint16_t TIMER = rgb(255, 190, 60);
constexpr uint16_t ALARM = rgb(255, 60, 60);
constexpr uint16_t TEXT = rgb(190, 190, 190);
constexpr uint16_t DIM = rgb(70, 70, 70);
}  // namespace color

constexpr int8_t DIGIT_DASH = 10;
constexpr int8_t DIGIT_BLANK = 11;

// Kresleni na displej 320x172 na sirku. Vsechno si pamatuje, co uz je nakreslene,
// a prekresluje jen zmeny, takze nic neblika.
class Ui {
 public:
  static constexpr uint8_t TEXT_ROWS = 7;
  static constexpr uint8_t TEXT_COLS = 26;

  void begin(bool rotated);
  void setRotated(bool rotated);
  void clear();
  void invert(bool on);

  void header(const char *title, uint16_t titleColor, const String &right, uint16_t rightColor);
  // big = 4 velke cislice kolem dvojtecky, small = 2 male vpravo (nullptr = bez nich, velke na stred)
  void digits(const int8_t big[4], bool colonOn, const int8_t small[2], bool dotOn, uint16_t col);
  void subText(const String &text, uint16_t col);
  void progress(int permille, uint16_t col);  // permille < 0 = schovat
  void textLine(uint8_t row, const String &text, uint16_t col);

 private:
  struct Slot {
    int16_t x, y, w, h;
    uint8_t t;
    uint8_t mask;
    uint16_t col;
    bool valid;
  };
  struct TextCache {
    String text;
    uint16_t col = 0;
    bool valid = false;
  };

  void layoutDigits(int16_t x0);
  void drawSlot(Slot &s, int8_t value, uint16_t col);
  void drawSegment(const Slot &s, uint8_t seg, uint16_t col);
  void hseg(int16_t xa, int16_t xb, int16_t cy, int16_t half, uint16_t col);
  void vseg(int16_t cx, int16_t ya, int16_t yb, int16_t half, uint16_t col);
  bool changed(TextCache &c, const String &text, uint16_t col);
  void print(int16_t x, int16_t y, const String &text, uint16_t col);

  Slot big_[4] = {};
  Slot small_[2] = {};
  int16_t digitsX0_ = -1;
  int8_t colonState_ = -1;
  int8_t dotState_ = -1;
  uint16_t colonCol_ = 0;
  uint16_t dotCol_ = 0;
  TextCache title_, right_, sub_, rows_[TEXT_ROWS];
  int16_t progressPx_ = -2;
  uint16_t progressCol_ = 0;
};
