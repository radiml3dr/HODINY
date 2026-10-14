#pragma once
#include <Arduino.h>

enum class BtnEvent { None, Click, DoubleClick, LongPress };

// Jedno tlacitko: klik, dvojklik a podrzeni.
// Click prijde hned po pusteni (i u prvniho kliku dvojkliku), DoubleClick po druhem.
class Button {
 public:
  void begin(uint8_t pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
  }

  BtnEvent poll(int64_t now) {
    bool raw = digitalRead(pin_) == LOW;
    if (raw != raw_) {
      raw_ = raw;
      rawChangedAt_ = now;
    }
    if (raw_ != down_ && now - rawChangedAt_ >= DEBOUNCE_MS) {
      down_ = raw_;
      if (down_) {
        pressedAt_ = rawChangedAt_;
        longFired_ = false;
      } else if (!longFired_) {
        if (lastClickAt_ >= 0 && pressedAt_ - lastClickAt_ <= DOUBLE_MS) {
          lastClickAt_ = -1;
          return BtnEvent::DoubleClick;
        }
        lastClickAt_ = now;
        return BtnEvent::Click;
      }
    }
    if (down_ && !longFired_ && now - pressedAt_ >= LONG_MS) {
      longFired_ = true;
      lastClickAt_ = -1;
      return BtnEvent::LongPress;
    }
    return BtnEvent::None;
  }

  bool down() const { return down_; }  // tlacitko je prave stisknute (po odruseni zakmitu)

  // Okamzik skutecneho stisku, aby stopky nemerily zpozdeni o debounce a pusteni
  int64_t pressedAt() const { return pressedAt_; }

 private:
  static constexpr int64_t DEBOUNCE_MS = 25;
  static constexpr int64_t DOUBLE_MS = 350;
  static constexpr int64_t LONG_MS = 700;

  uint8_t pin_ = 0;
  bool raw_ = false;
  bool down_ = false;
  bool longFired_ = false;
  int64_t rawChangedAt_ = 0;
  int64_t pressedAt_ = 0;
  int64_t lastClickAt_ = -1;
};
