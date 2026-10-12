#include "ui.h"

#include <Arduino_GFX_Library.h>

#include "config.h"
#include "font_japan.h"
#include "font_klingon.h"

namespace {

// Pismo z bitmap: 10 cislic za sebou, kazda w/8*h bajtu
struct BitmapFont {
  const uint8_t *big;
  int16_t bigW, bigH;
  const uint8_t *small;
  int16_t smallW, smallH;
};
constexpr BitmapFont KLINGON_FONT = {KLINGON_BIG[0], KLINGON_BIG_W, KLINGON_BIG_H,
                                     KLINGON_SMALL[0], KLINGON_SMALL_W, KLINGON_SMALL_H};
constexpr BitmapFont JAPAN_FONT = {JAPAN_BIG[0], JAPAN_BIG_W, JAPAN_BIG_H,
                                   JAPAN_SMALL[0], JAPAN_SMALL_W, JAPAN_SMALL_H};

constexpr int16_t W = 320;

constexpr int16_t MARGIN_X = 4;
constexpr int16_t CHAR_W = 12;  // vestaveny font 6x8 ve dvojnasobne velikosti
constexpr int16_t HEADER_Y = 3;
constexpr int16_t HEADER_INSET = 16;  // displej ma kulate rohy, text u kraje by v nich zmizel
constexpr int16_t RULE_Y = 23;
constexpr uint8_t TITLE_COLS = 15;
constexpr uint8_t RIGHT_COLS = 8;

constexpr int16_t BIG_Y = 32;
constexpr int16_t BIG_W = 50;
constexpr int16_t BIG_H = 96;
constexpr uint8_t BIG_T = 11;
constexpr int16_t SMALL_W = 26;
constexpr int16_t SMALL_H = 48;
constexpr uint8_t SMALL_T = 7;

// Rozlozeni cislic relativne k levemu okraji skupiny: DD:DD .dd
constexpr int16_t BIG_DX[4] = {0, 58, 134, 192};
constexpr int16_t COLON_DX = 116;
constexpr int16_t COLON_SIZE = 10;
constexpr int16_t GROUP_W = 242;  // jen velke cislice
constexpr int16_t DOT_DX = 245;
constexpr int16_t DOT_SIZE = 6;
constexpr int16_t SMALL_DX[2] = {254, 285};
constexpr int16_t FULL_W = 311;  // vcetne malych

constexpr int16_t SUB_Y = 140;
constexpr int16_t BAR_Y = 163;
constexpr int16_t BAR_H = 6;
constexpr int16_t BAR_W = W - 2 * MARGIN_X;

constexpr int16_t ROW_Y0 = 30;
constexpr int16_t ROW_PITCH = 20;

constexpr int16_t SEG_GAP = 2;

// bity: A=1 B=2 C=4 D=8 E=16 F=32 G=64; posledni dve jsou pomlcka a prazdno
constexpr uint8_t SEGMENTS[12] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40, 0x00};

// Predatorska cislice je 7 car: svisla nahore, dve vodorovne a ctyri sikme do kosoctverce.
// Souradnice jsou v promile oblasti znaku, bity masky v poradi tabulky.
struct Stroke {
  int16_t u0, v0, u1, v1;
};
constexpr Stroke PREDATOR_STROKES[7] = {
    {500, 0, 500, 330},     // svisla
    {0, 290, 330, 290},     // vodorovna leva
    {670, 290, 1000, 290},  // vodorovna prava
    {320, 385, 80, 636},    // horni leva /
    {680, 385, 920, 636},   // horni prava \.
    {80, 748, 320, 1000},   // dolni leva \.
    {920, 748, 680, 1000},  // dolni prava /
};
// prevzato z fanouskovskeho fontu Predator (Giedi Prime); posledni dve jsou pomlcka a prazdno
constexpr uint8_t PREDATOR_SEGMENTS[12] = {0x7F, 0x0F, 0x3F, 0x24, 0x3C, 0x6B, 0x78, 0x4B, 0x67, 0x79, 0x06, 0x00};

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;

String padRight(const String &s, uint8_t n) {
  String out = s.substring(0, n);
  while (out.length() < n) out += ' ';
  return out;
}

String padLeft(const String &s, uint8_t n) {
  String out = s.substring(0, n);
  while (out.length() < n) out = ' ' + out;
  return out;
}

String padCenter(const String &s, uint8_t n) {
  String out = s.substring(0, n);
  uint8_t left = (n - out.length()) / 2;
  return padRight(padLeft(out, out.length() + left), n);
}

}  // namespace

void Ui::begin(bool rotated) {
  bus = new Arduino_HWSPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI);
  // panel 172x320 je v pameti radice posunuty o 34 sloupcu
  gfx = new Arduino_ST7789(bus, PIN_LCD_RST, rotated ? 3 : 1, true, 172, 320, 34, 0, 34, 0);
  gfx->begin(40000000);
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  clear();
  analogWrite(PIN_LCD_BL, BACKLIGHT);
}

void Ui::setRotated(bool rotated) {
  gfx->setRotation(rotated ? 3 : 1);
  clear();
}

void Ui::sleep() {
  // PWM se ve spanku zastavi, proto podsviceni natvrdo do nuly
  ledcDetach(PIN_LCD_BL);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);
  gfx->displayOff();
}

void Ui::setFont(DigitFont font) {
  font_ = font;
  clear();
}

void Ui::clear() {
  gfx->fillScreen(color::BG);
  gfx->drawFastHLine(0, RULE_Y, W, color::DIM);
  for (Slot &s : big_) s.valid = false;
  for (Slot &s : small_) s.valid = false;
  digitsX0_ = -1;
  colonState_ = -1;
  dotState_ = -1;
  title_.valid = false;
  right_.valid = false;
  sub_.valid = false;
  for (TextCache &c : rows_) c.valid = false;
  progressPx_ = -2;
}

void Ui::invert(bool on) { gfx->invertDisplay(on); }

bool Ui::changed(TextCache &c, const String &text, uint16_t col) {
  if (c.valid && c.col == col && c.text == text) return false;
  c.text = text;
  c.col = col;
  c.valid = true;
  return true;
}

void Ui::print(int16_t x, int16_t y, const String &text, uint16_t col) {
  gfx->setTextColor(col, color::BG);
  gfx->setCursor(x, y);
  gfx->print(text);
}

void Ui::header(const char *title, uint16_t titleColor, const String &right, uint16_t rightColor) {
  if (changed(title_, title, titleColor)) {
    print(HEADER_INSET, HEADER_Y, padRight(title, TITLE_COLS), titleColor);
  }
  if (changed(right_, right, rightColor)) {
    print(W - HEADER_INSET - RIGHT_COLS * CHAR_W, HEADER_Y, padLeft(right, RIGHT_COLS), rightColor);
  }
}

void Ui::subText(const String &text, uint16_t col) {
  if (changed(sub_, text, col)) {
    print(MARGIN_X, SUB_Y, padCenter(text, TEXT_COLS), col);
  }
}

void Ui::textLine(uint8_t row, const String &text, uint16_t col) {
  if (row >= TEXT_ROWS) return;
  if (changed(rows_[row], text, col)) {
    print(MARGIN_X, ROW_Y0 + row * ROW_PITCH, padRight(text, TEXT_COLS), col);
  }
}

void Ui::progress(int permille, uint16_t col) {
  int16_t px = permille < 0 ? -1 : (int32_t)constrain(permille, 0, 1000) * BAR_W / 1000;
  if (px == progressPx_ && col == progressCol_) return;
  progressPx_ = px;
  progressCol_ = col;
  if (px < 0) {
    gfx->fillRect(MARGIN_X, BAR_Y, BAR_W, BAR_H, color::BG);
    return;
  }
  gfx->fillRect(MARGIN_X, BAR_Y, px, BAR_H, col);
  gfx->fillRect(MARGIN_X + px, BAR_Y, BAR_W - px, BAR_H, color::SEG_OFF);
}

void Ui::layoutDigits(int16_t x0) {
  digitsX0_ = x0;
  gfx->fillRect(0, BIG_Y, W, BIG_H, color::BG);
  for (uint8_t i = 0; i < 4; i++) {
    big_[i] = {(int16_t)(x0 + BIG_DX[i]), BIG_Y, BIG_W, BIG_H, BIG_T, 0, 0, 0, false};
  }
  for (uint8_t i = 0; i < 2; i++) {
    small_[i] = {(int16_t)(x0 + SMALL_DX[i]), BIG_Y + BIG_H - SMALL_H, SMALL_W, SMALL_H, SMALL_T, 0, 0, 0, false};
  }
  colonState_ = -1;
  dotState_ = -1;
}

void Ui::digits(const int8_t big[4], bool colonOn, const int8_t small[2], bool dotOn, uint16_t col) {
  int16_t x0 = (W - (small ? FULL_W : GROUP_W)) / 2;
  if (x0 != digitsX0_) layoutDigits(x0);

  for (uint8_t i = 0; i < 4; i++) drawSlot(big_[i], big[i], col);

  if (colonState_ != colonOn || (colonOn && colonCol_ != col)) {
    colonState_ = colonOn;
    colonCol_ = col;
    uint16_t c = colonOn ? col : color::SEG_OFF;
    gfx->fillRect(x0 + COLON_DX, BIG_Y + BIG_H / 3 - COLON_SIZE / 2, COLON_SIZE, COLON_SIZE, c);
    gfx->fillRect(x0 + COLON_DX, BIG_Y + 2 * BIG_H / 3 - COLON_SIZE / 2, COLON_SIZE, COLON_SIZE, c);
  }

  if (!small) return;
  for (uint8_t i = 0; i < 2; i++) drawSlot(small_[i], small[i], col);
  if (dotState_ != dotOn || (dotOn && dotCol_ != col)) {
    dotState_ = dotOn;
    dotCol_ = col;
    gfx->fillRect(x0 + DOT_DX, BIG_Y + BIG_H - DOT_SIZE, DOT_SIZE, DOT_SIZE, dotOn ? col : color::BG);
  }
}

void Ui::drawSlot(Slot &s, int8_t value, uint16_t col) {
  value = constrain(value, 0, DIGIT_BLANK);
  uint8_t mask = 0;
  if (font_ == DigitFont::Klingon || font_ == DigitFont::Japan) {
    if (!s.valid || value != s.value || col != s.col) drawBitmapDigit(s, value, col);
  } else {
    bool predator = font_ == DigitFont::Predator;
    mask = predator ? PREDATOR_SEGMENTS[value] : SEGMENTS[value];
    for (uint8_t seg = 0; seg < 7; seg++) {
      bool on = mask & (1 << seg);
      bool wasOn = s.mask & (1 << seg);
      if (!s.valid || on != wasOn || (on && col != s.col)) {
        uint16_t c = on ? col : color::SEG_OFF;
        if (predator) drawPredatorSegment(s, seg, c);
        else drawSegment(s, seg, c);
      }
    }
  }
  s.mask = mask;
  s.value = value;
  s.col = col;
  s.valid = true;
}

// Predatorske znaky jsou skoro ctvercove, proto nezabiraji celou vysku mista
void Ui::drawPredatorSegment(const Slot &s, uint8_t seg, uint16_t col) {
  float r = s.t * 0.22f;
  float areaH = s.h * 0.85f;
  float left = s.x + r;
  float top = s.y + (s.h - areaH) / 2 + r;
  float spanX = (s.w - 1 - 2 * r) / 1000.0f;
  float spanY = (areaH - 1 - 2 * r) / 1000.0f;
  const Stroke &k = PREDATOR_STROKES[seg];
  float x0 = left + k.u0 * spanX, y0 = top + k.v0 * spanY;
  float x1 = left + k.u1 * spanX, y1 = top + k.v1 * spanY;

  // tlusta cara jako ctyruhelnik ze dvou trojuhelniku
  float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  float nx = -(y1 - y0) / len * r;
  float ny = (x1 - x0) / len * r;
  int16_t ax = lroundf(x0 + nx), ay = lroundf(y0 + ny);
  int16_t bx = lroundf(x0 - nx), by = lroundf(y0 - ny);
  int16_t cx = lroundf(x1 - nx), cy = lroundf(y1 - ny);
  int16_t dx = lroundf(x1 + nx), dy = lroundf(y1 + ny);
  gfx->fillTriangle(ax, ay, bx, by, cx, cy, col);
  gfx->fillTriangle(ax, ay, cx, cy, dx, dy, col);
}

void Ui::drawBitmapDigit(const Slot &s, int8_t value, uint16_t col) {
  const BitmapFont &f = font_ == DigitFont::Japan ? JAPAN_FONT : KLINGON_FONT;
  bool big = s.h == BIG_H;
  int16_t w = big ? f.bigW : f.smallW;
  int16_t h = big ? f.bigH : f.smallH;
  int16_t x = s.x + (s.w - w) / 2;
  int16_t y = s.y + (s.h - h) / 2;
  if (value <= 9) {
    const uint8_t *glyphs = big ? f.big : f.small;
    gfx->drawBitmap(x, y, glyphs + value * (w / 8 * h), w, h, col, color::BG);
    return;
  }
  gfx->fillRect(x, y, w, h, color::BG);
  if (value == DIGIT_DASH) gfx->fillRect(x + w / 4, y + h / 2 - s.t / 4, w / 2, s.t / 2, col);
}

void Ui::drawSegment(const Slot &s, uint8_t seg, uint16_t col) {
  int16_t half = s.t / 2;
  int16_t xl = s.x + half;
  int16_t xr = s.x + s.w - 1 - half;
  int16_t yt = s.y + half;
  int16_t ym = s.y + s.h / 2;
  int16_t yb = s.y + s.h - 1 - half;
  switch (seg) {
    case 0: hseg(xl + SEG_GAP, xr - SEG_GAP, yt, half, col); break;  // A
    case 1: vseg(xr, yt + SEG_GAP, ym - SEG_GAP, half, col); break;  // B
    case 2: vseg(xr, ym + SEG_GAP, yb - SEG_GAP, half, col); break;  // C
    case 3: hseg(xl + SEG_GAP, xr - SEG_GAP, yb, half, col); break;  // D
    case 4: vseg(xl, ym + SEG_GAP, yb - SEG_GAP, half, col); break;  // E
    case 5: vseg(xl, yt + SEG_GAP, ym - SEG_GAP, half, col); break;  // F
    case 6: hseg(xl + SEG_GAP, xr - SEG_GAP, ym, half, col); break;  // G
  }
}

// Segment je sestiuhelnik: obdelnik se spickami na obou koncich
void Ui::hseg(int16_t xa, int16_t xb, int16_t cy, int16_t half, uint16_t col) {
  gfx->fillRect(xa + half, cy - half, xb - xa - 2 * half + 1, 2 * half + 1, col);
  gfx->fillTriangle(xa, cy, xa + half, cy - half, xa + half, cy + half, col);
  gfx->fillTriangle(xb, cy, xb - half, cy - half, xb - half, cy + half, col);
}

void Ui::vseg(int16_t cx, int16_t ya, int16_t yb, int16_t half, uint16_t col) {
  gfx->fillRect(cx - half, ya + half, 2 * half + 1, yb - ya - 2 * half + 1, col);
  gfx->fillTriangle(cx, ya, cx - half, ya + half, cx + half, ya + half, col);
  gfx->fillTriangle(cx, yb, cx - half, yb - half, cx + half, yb - half, col);
}
