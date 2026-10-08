#pragma once
#include <Arduino.h>

// Vsechny casy jsou v milisekundach od startu (esp_timer), ne ze systemovych hodin,
// takze je neovlivni synchronizace casu pres NTP.

struct Stopwatch {
  bool running = false;
  int64_t startedAt = 0;
  int64_t accumulated = 0;

  int64_t elapsed(int64_t now) const { return accumulated + (running ? now - startedAt : 0); }

  void toggle(int64_t at) {
    if (running) accumulated += at - startedAt;
    else startedAt = at;
    running = !running;
  }

  void reset() {
    running = false;
    accumulated = 0;
  }
};

struct Countdown {
  bool running = false;
  int64_t totalMs = 0;
  int64_t remainMs = 0;
  int64_t endsAt = 0;

  void setTotal(int64_t ms) {
    totalMs = ms;
    reset();
  }

  void reset() {
    running = false;
    remainMs = totalMs;
  }

  int64_t remaining(int64_t now) const {
    if (!running) return remainMs;
    return endsAt > now ? endsAt - now : 0;
  }

  bool idle() const { return !running && remainMs == totalMs; }
  bool finished() const { return !running && remainMs == 0; }

  void toggle(int64_t at) {
    if (running) {
      remainMs = remaining(at);
      running = false;
    } else if (remainMs > 0) {
      endsAt = at + remainMs;
      running = true;
    }
  }

  // true prave jednou, ve chvili kdy odpocet dobehne
  bool expired(int64_t now) {
    if (!running || now < endsAt) return false;
    running = false;
    remainMs = 0;
    return true;
  }
};
