// Hodiny, stopky a minutka pro LAFVIN ESP32-C6 s 1,47" displejem.
// Ovladani jednim tlacitkem BOOT: klik, dvojklik, podrzeni (dalsi rezim).

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "button.h"
#include "config.h"
#include "timing.h"
#include "ui.h"

namespace {

enum class Mode { Clock, Stopwatch, Timer, Font, Off };

Ui ui;
Button button;
Preferences prefs;
Stopwatch stopwatch;
Countdown countdown;

Mode mode = Mode::Timer;
int64_t lastActivityAt = 0;  // posledni stisk nebo zmena rezimu, pro navrat na hodiny
int64_t lastBusyAt = 0;      // posledni stisk nebo bezici mereni, pro samocinne vypnuti
bool autoOffEnabled = true;  // setreni jde zrusit na obrazovce VYPNOUT; plati do restartu
int64_t pendingOffAt = 0;    // klik na VYPNOUT ceka, jestli z nej nebude dvojklik
bool showInfo = false;
bool rotated = false;
DigitFont digitFont = DigitFont::Latin;

// stav pred prvnim klikem, aby dvojklik vedel, co prvni klik zmenil
bool stopwatchRanBeforeClick = false;
bool timerIdleBeforeClick = false;

bool alarmOn = false;
bool alarmPhase = false;
int64_t alarmStartedAt = 0;

const String cfgSsid = WIFI_SSID;
const String cfgPass = WIFI_PASS;
bool wifiOn = false;
bool sntpStarted = false;
bool everSynced = false;
int64_t wifiStartedAt = 0;
int64_t nextSyncAt = 0;
volatile bool ntpSynced = false;

const char *const DAYS[] = {"NEDELE", "PONDELI", "UTERY", "STREDA", "CTVRTEK", "PATEK", "SOBOTA"};

int64_t nowMs() { return esp_timer_get_time() / 1000; }

bool timeValid() { return time(nullptr) > 1700000000; }

bool localNow(struct tm &t) {
  if (!timeValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

String clockText(int offsetSec = 0) {
  if (!timeValid()) return "";
  time_t at = time(nullptr) + offsetSec;
  struct tm t;
  localtime_r(&at, &t);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return buf;
}

// ---------------------------------------------------------------- minutka

// Uklada se, aby po resetu zustal posledni nastaveny cas
void setTimerTotal(uint32_t ms) {
  countdown.setTotal(ms);
  prefs.putUInt("tms", ms);
}

void nextPreset() {
  uint32_t next = TIMER_PRESETS_SEC[0] * 1000UL;
  for (uint16_t seconds : TIMER_PRESETS_SEC) {
    if (seconds * 1000LL > countdown.totalMs) {
      next = seconds * 1000UL;
      break;
    }
  }
  setTimerTotal(next);
}

void setMode(Mode m) {
  mode = m;
  showInfo = false;
  pendingOffAt = 0;
  lastActivityAt = nowMs();
  ui.clear();
}

void startAlarm(int64_t now) {
  alarmOn = true;
  alarmPhase = false;
  alarmStartedAt = now;
  setMode(Mode::Timer);
}

void stopAlarm() {
  if (!alarmOn) return;
  alarmOn = false;
  alarmPhase = false;
  lastActivityAt = nowMs();
  ui.invert(false);
  rgbLedWrite(PIN_LED, 0, 0, 0);
}

// Deska nema bzucak, takze konec odpoctu hlasi blikanim displeje a RGB LED
void updateAlarm(int64_t now) {
  if (!alarmOn) return;
  if (now - alarmStartedAt >= ALARM_DURATION_MS) {
    stopAlarm();
    return;
  }
  bool phase = ((now - alarmStartedAt) / 350) % 2 == 0;
  if (phase == alarmPhase) return;
  alarmPhase = phase;
  ui.invert(phase);
  uint8_t level = phase ? 80 : 0;
  rgbLedWrite(PIN_LED, level, level, level);
}

// ---------------------------------------------------------------- tlacitko

// "Vypnuti" je lehky spanek: displej, podsviceni i WiFi zhasnou a procesor stoji, dokud nekdo
// nestiskne tlacitko. Hluboky spanek by setril vic, ale tlacitko BOOT (GPIO9) z nej budit neumi.
// Po probuzeni se hodiny restartuji, takze nabehnou stejne jako po zapnuti.
void powerOff() {
  Serial.println("[spanek] vypinam, probudi stisk tlacitka");
  delay(50);
  stopAlarm();
  rgbLedWrite(PIN_LED, 0, 0, 0);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  ui.sleep();

  while (digitalRead(PIN_BUTTON) == LOW) delay(10);
  gpio_wakeup_enable((gpio_num_t)PIN_BUTTON, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();

  // restart az po pusteni: GPIO9 zaroven pri resetu voli rezim nahravani
  while (digitalRead(PIN_BUTTON) == LOW) delay(10);
  esp_restart();
}

void autoPowerOff(int64_t now) {
  if (pendingOffAt && now >= pendingOffAt) {
    if (digitalRead(PIN_BUTTON) == LOW) {
      pendingOffAt = now + 100;  // druhy stisk jeste trva, pockam na dvojklik
    } else {
      pendingOffAt = 0;
      if (mode == Mode::Off) powerOff();
    }
  }
  if (!autoOffEnabled) return;
  if (countdown.running || stopwatch.running || alarmOn) lastBusyAt = now;
  if (now - lastBusyAt < AUTO_OFF_MS) return;
  if (HWCDC::isPlugged()) {
    // u pocitace se nevypina; odklad, at se stav USB necte v kazdem pruchodu smycky
    lastBusyAt = now - AUTO_OFF_MS + 10000;
    return;
  }
  powerOff();
}

// Minutka, ktera neodpocitava, a stojici stopky po chvili necinnosti uvolni displej hodinam
void autoReturnToClock(int64_t now) {
  int64_t idle = now - lastActivityAt;
  bool timerIdle = mode == Mode::Timer && !countdown.running && !alarmOn && idle >= TIMER_IDLE_RETURN_MS;
  bool stopwatchIdle = mode == Mode::Stopwatch && !stopwatch.running && idle >= STOPWATCH_IDLE_RETURN_MS;
  bool fontIdle = (mode == Mode::Font || mode == Mode::Off) && idle >= TIMER_IDLE_RETURN_MS;
  if (timerIdle || stopwatchIdle || fontIdle) setMode(Mode::Clock);
}

void onButton(BtnEvent ev, int64_t at) {
  lastActivityAt = lastBusyAt = nowMs();
  if (alarmOn) {
    stopAlarm();
    countdown.reset();
    timerIdleBeforeClick = false;  // pripadny druhy klik uz nema prepnout predvolbu
    return;
  }
  if (ev == BtnEvent::LongPress) {
    setMode(mode == Mode::Clock ? Mode::Stopwatch : mode == Mode::Stopwatch ? Mode::Timer : mode == Mode::Timer ? Mode::Font : mode == Mode::Font ? Mode::Off : Mode::Clock);
    return;
  }
  switch (mode) {
    case Mode::Off:
      if (ev == BtnEvent::Click) {
        pendingOffAt = nowMs() + 500;
      } else {
        autoOffEnabled = !autoOffEnabled;
        Serial.printf("[spanek] setreni %s\n", autoOffEnabled ? "zapnuto" : "zruseno");
        setMode(Mode::Clock);
      }
      break;

    case Mode::Font:
      if (ev == BtnEvent::Click) {
        // pismo se neuklada, po resetu je vzdy Latin
        digitFont = (DigitFont)(((uint8_t)digitFont + 1) % DIGIT_FONT_COUNT);
        ui.setFont(digitFont);
      }
      break;

    case Mode::Clock:
      if (ev == BtnEvent::Click) {
        showInfo = !showInfo;
        ui.clear();
      }
      break;

    case Mode::Stopwatch:
      if (ev == BtnEvent::Click) {
        stopwatchRanBeforeClick = stopwatch.running;
        stopwatch.toggle(at);
      } else if (!stopwatchRanBeforeClick) {
        // nulovat jde jen ze zastavenych stopek, at o namereny cas neprijdu preklepem
        stopwatch.reset();
      }
      break;

    case Mode::Timer:
      if (ev == BtnEvent::Click) {
        timerIdleBeforeClick = countdown.idle();
        if (countdown.finished()) countdown.reset();
        else countdown.toggle(at);
      } else if (timerIdleBeforeClick) {
        nextPreset();
      } else {
        countdown.reset();
      }
      break;
  }
}

// ---------------------------------------------------------------- kresleni

void drawInfo() {
  ui.header("SIT", color::TEXT, "", color::TEXT);
  ui.textLine(0, "WiFi: " + cfgSsid, color::TEXT);
  ui.textLine(1, wifiOn ? "stav: hledam cas..." : "stav: vypnuta, setrim", color::TEXT);
  // po probuzeni ze spanku cas bezi dal i bez nove synchronizace
  ui.textLine(2, everSynced ? "cas:  z internetu" : timeValid() ? "cas:  z minula" : "cas:  nenastaven", color::TEXT);
  ui.textLine(6, "klik zpet, drzet rezim", color::DIM);
}

void drawClock() {
  ui.header("HODINY", color::CLOCK, wifiOn ? "WiFi" : autoOffEnabled ? "" : "NESPIM", color::TIMER);
  ui.progress(-1, color::CLOCK);

  struct tm t;
  if (!localNow(t)) {
    const int8_t dashes[4] = {DIGIT_DASH, DIGIT_DASH, DIGIT_DASH, DIGIT_DASH};
    const int8_t blank[2] = {DIGIT_BLANK, DIGIT_BLANK};
    ui.digits(dashes, true, blank, false, color::CLOCK);
    ui.subText(wifiOn ? "cekam na cas z WiFi" : "WiFi nedostupna", color::TEXT);
    return;
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const int8_t big[4] = {(int8_t)(t.tm_hour / 10), (int8_t)(t.tm_hour % 10), (int8_t)(t.tm_min / 10), (int8_t)(t.tm_min % 10)};
  const int8_t small[2] = {(int8_t)(t.tm_sec / 10), (int8_t)(t.tm_sec % 10)};
  ui.digits(big, tv.tv_usec < 500000, small, false, color::CLOCK);

  char date[Ui::TEXT_COLS + 1];
  snprintf(date, sizeof(date), "%s %d.%d.%d", DAYS[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  ui.subText(date, color::TEXT);
}

void drawStopwatch(int64_t now) {
  ui.header("STOPKY", color::STOPWATCH, clockText(), color::TEXT);
  ui.progress(-1, color::STOPWATCH);

  int64_t elapsed = stopwatch.elapsed(now);
  int cs = (elapsed / 10) % 100;
  int s = (elapsed / 1000) % 60;
  int64_t m = elapsed / 60000;
  if (m < 100) {
    const int8_t big[4] = {(int8_t)(m / 10), (int8_t)(m % 10), (int8_t)(s / 10), (int8_t)(s % 10)};
    const int8_t small[2] = {(int8_t)(cs / 10), (int8_t)(cs % 10)};
    ui.digits(big, true, small, true, color::STOPWATCH);
  } else {
    // po 100 minutach hodiny:minuty a male sekundy
    int h = min<int64_t>(m / 60, 99);
    int mm = m % 60;
    const int8_t big[4] = {(int8_t)(h / 10), (int8_t)(h % 10), (int8_t)(mm / 10), (int8_t)(mm % 10)};
    const int8_t small[2] = {(int8_t)(s / 10), (int8_t)(s % 10)};
    ui.digits(big, true, small, false, color::STOPWATCH);
  }

  if (stopwatch.running) ui.subText("klik = stop", color::TEXT);
  else if (elapsed == 0) ui.subText("klik = start", color::TEXT);
  else ui.subText("klik = dal   2x = nula", color::TEXT);
}

void drawTimer(int64_t now) {
  uint16_t col = alarmOn || countdown.finished() ? color::ALARM : color::TIMER;
  ui.header("MINUTKA", color::TIMER, clockText(), color::TEXT);

  int64_t remaining = countdown.remaining(now);
  int secs = (remaining + 999) / 1000;  // nahoru, aby 00:00 znamenalo opravdu konec
  int m = secs / 60;
  int s = secs % 60;
  const int8_t big[4] = {(int8_t)(m / 10), (int8_t)(m % 10), (int8_t)(s / 10), (int8_t)(s % 10)};
  ui.digits(big, true, nullptr, false, col);
  ui.progress(remaining * 1000 / countdown.totalMs, col);

  if (alarmOn) {
    ui.subText("HOTOVO!  klik = konec", col);
  } else if (countdown.finished()) {
    ui.subText("HOTOVO   klik = znovu", col);
  } else if (countdown.running) {
    String end = clockText(secs);
    ui.subText(end.isEmpty() ? String("klik = pauza") : "konec v " + end, color::TEXT);
  } else if (countdown.idle()) {
    ui.subText("klik=start 2x=jiny cas", color::TEXT);
  } else {
    ui.subText("PAUZA   2x = zrusit", color::TEXT);
  }
}

// Vyber pisma cislic; ukazka ma stejne rozlozeni jako hodiny
void drawFont() {
  static const char *const NAMES[DIGIT_FONT_COUNT] = {"LATIN", "KLINGON", "PREDATOR", "JAPAN"};
  ui.header("PISMO", color::TEXT, clockText(), color::TEXT);
  ui.progress(-1, color::CLOCK);
  const int8_t big[4] = {1, 2, 3, 4};
  const int8_t small[2] = {5, 6};
  ui.digits(big, true, small, false, color::CLOCK);
  ui.subText(String(NAMES[(uint8_t)digitFont]) + "  klik = dalsi", color::TEXT);
}

void drawOff() {
  ui.header("VYPNOUT", color::ALARM, clockText(), color::TEXT);
  ui.textLine(1, "klik = vypnout", color::TEXT);
  ui.textLine(2, "zapnuti: stisk tlacitka", color::DIM);
  ui.textLine(4, autoOffEnabled ? "2x = zrusit setreni" : "2x = zapnout setreni", color::TEXT);
  ui.textLine(5, autoOffEnabled ? "vypne se po 10 min" : "samo se nevypne", color::DIM);
}

void draw(int64_t now) {
  switch (mode) {
    case Mode::Off: drawOff(); break;
    case Mode::Font: drawFont(); break;
    case Mode::Clock:
      if (showInfo) drawInfo();
      else drawClock();
      break;
    case Mode::Stopwatch: drawStopwatch(now); break;
    case Mode::Timer: drawTimer(now); break;
  }
}

// ---------------------------------------------------------------- sit

// WiFi slouzi jen k nastaveni casu: zapne se, pocka na NTP a zase se vypne, aby nezrala baterii.

void onNtpSync(struct timeval *) { ntpSynced = true; }

void wifiStart(int64_t now) {
  ntpSynced = false;
  sntpStarted = false;
  wifiOn = true;
  wifiStartedAt = now;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.printf("[wifi] zapinam, pripojuji se k '%s'\n", cfgSsid.c_str());
}

void wifiStop(int64_t now, bool synced) {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
  nextSyncAt = now + (synced ? NTP_RESYNC_MS : NTP_RETRY_MS);
  Serial.printf("[wifi] vypinam, cas %s\n", synced ? "nastaven z internetu" : "se nepodarilo ziskat");
}

void netBegin(int64_t now) {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  sntp_set_time_sync_notification_cb(onNtpSync);
  wifiStart(now);
}

void netLoop(int64_t now) {
  if (!wifiOn) {
    if (now >= nextSyncAt) wifiStart(now);
    return;
  }
  if (!sntpStarted && WiFi.status() == WL_CONNECTED) {
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    sntpStarted = true;
  }
  if (ntpSynced) {
    everSynced = true;
    wifiStop(now, true);
  } else if (now - wifiStartedAt > NTP_TIMEOUT_MS) {
    wifiStop(now, false);
  }
}

// ---------------------------------------------------------------- seriova linka

// k = klik, d = dvojklik (vcetne prvniho kliku), p = podrzeni; hodi se pri ladeni bez sahani na desku
void pollSerial(int64_t now) {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'k': onButton(BtnEvent::Click, now); break;
      case 'd':
        onButton(BtnEvent::Click, now);
        onButton(BtnEvent::DoubleClick, now);
        break;
      case 'p': onButton(BtnEvent::LongPress, now); break;
    }
  }
}

void printStatus(int64_t now) {
  static const char *const MODES[] = {"hodiny", "stopky", "minutka", "pismo", "vypnout"};
  struct tm t;
  char clock[20] = "nenastaven";
  if (localNow(t)) strftime(clock, sizeof(clock), "%H:%M:%S", &t);
  Serial.printf("[stav] beh=%llds reset=%d pismo=%d rezim=%s cas=%s stopky=%lld ms%s minutka=%lld/%lld ms%s wifi=%s sync=%d setreni=%d heap=%u\n",
                now / 1000, (int)esp_reset_reason(), (int)digitFont, MODES[(int)mode], clock, stopwatch.elapsed(now),
                stopwatch.running ? " (bezi)" : "", countdown.remaining(now), countdown.totalMs,
                alarmOn ? " (ALARM)" : countdown.running ? " (bezi)" : "",
                wifiOn ? "zap" : "vyp", everSynced, autoOffEnabled,
                (unsigned)ESP.getFreeHeap());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  rgbLedWrite(PIN_LED, 0, 0, 0);

  prefs.begin("hodiny");
  rotated = prefs.getBool("rot", false);
  uint32_t timerMs = prefs.getUInt("tms", TIMER_PRESETS_SEC[0] * 1000UL);
  if (timerMs < 1000 || timerMs > TIMER_MAX_MS) timerMs = TIMER_PRESETS_SEC[0] * 1000UL;
  countdown.setTotal(timerMs);
  lastActivityAt = nowMs();

  button.begin(PIN_BUTTON);
  ui.begin(rotated);

  setenv("TZ", TIMEZONE, 1);
  tzset();
  netBegin(nowMs());
  Serial.println("[start] hodiny bezi");
}

void loop() {
  static int64_t lastNetAt = 0, lastDrawAt = 0, lastStatusAt = 0;
  int64_t now = nowMs();

  BtnEvent ev = button.poll(now);
  if (ev != BtnEvent::None) {
    Serial.printf("[tlacitko] %s\n", ev == BtnEvent::Click ? "klik" : ev == BtnEvent::DoubleClick ? "dvojklik" : "podrzeni");
    onButton(ev, button.pressedAt());
  }
  pollSerial(now);

  if (countdown.expired(now)) startAlarm(now);
  updateAlarm(now);
  autoReturnToClock(now);
  autoPowerOff(now);

  if (now - lastNetAt >= 1000) {
    lastNetAt = now;
    netLoop(now);
  }

  if (now - lastDrawAt >= 20) {
    lastDrawAt = now;
    draw(now);
  }
  if (now - lastStatusAt >= 10000) {
    lastStatusAt = now;
    printStatus(now);
  }
  delay(1);
}
