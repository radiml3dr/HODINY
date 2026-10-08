// Hodiny, stopky a minutka pro LAFVIN ESP32-C6 s 1,47" displejem.
// Ovladani jednim tlacitkem BOOT: klik, dvojklik, podrzeni (dalsi rezim).

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "button.h"
#include "config.h"
#include "timing.h"
#include "ui.h"

namespace {

enum class Mode { Clock, Stopwatch, Timer };

Ui ui;
Button button;
Preferences prefs;
WebServer server(80);
Stopwatch stopwatch;
Countdown countdown;

Mode mode = Mode::Timer;
int64_t lastActivityAt = 0;  // posledni stisk nebo zmena rezimu, pro navrat na hodiny
bool showInfo = false;
bool rotated = false;

// stav pred prvnim klikem, aby dvojklik vedel, co prvni klik zmenil
bool stopwatchRanBeforeClick = false;
bool timerIdleBeforeClick = false;

bool alarmOn = false;
bool alarmPhase = false;
int64_t alarmStartedAt = 0;

const String cfgSsid = WIFI_SSID;
const String cfgPass = WIFI_PASS;
bool apOn = false;
bool staWasConnected = false;
bool mdnsStarted = false;
int64_t staAttemptAt = 0;
volatile bool ntpSynced = false;
bool browserTimeSet = false;

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

// Minutka, ktera neodpocitava, a stojici stopky po chvili necinnosti uvolni displej hodinam
void autoReturnToClock(int64_t now) {
  int64_t idle = now - lastActivityAt;
  bool timerIdle = mode == Mode::Timer && !countdown.running && !alarmOn && idle >= TIMER_IDLE_RETURN_MS;
  bool stopwatchIdle = mode == Mode::Stopwatch && !stopwatch.running && idle >= STOPWATCH_IDLE_RETURN_MS;
  if (timerIdle || stopwatchIdle) setMode(Mode::Clock);
}

void onButton(BtnEvent ev, int64_t at) {
  lastActivityAt = nowMs();
  if (alarmOn) {
    stopAlarm();
    countdown.reset();
    timerIdleBeforeClick = false;  // pripadny druhy klik uz nema prepnout predvolbu
    return;
  }
  if (ev == BtnEvent::LongPress) {
    setMode(mode == Mode::Clock ? Mode::Stopwatch : mode == Mode::Stopwatch ? Mode::Timer : Mode::Clock);
    return;
  }
  switch (mode) {
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
  bool connected = WiFi.status() == WL_CONNECTED;
  ui.header("NASTAVENI", color::TEXT, "", color::TEXT);
  ui.textLine(0, "WiFi: " + cfgSsid, color::TEXT);
  ui.textLine(1, "IP:   " + (connected ? WiFi.localIP().toString() : String("nepripojeno")), color::TEXT);
  ui.textLine(2, apOn ? String("AP:   ") + AP_SSID : String("AP:   vypnuto"), color::TIMER);
  ui.textLine(3, apOn ? String("heslo ") + AP_PASS : String(""), color::TIMER);
  ui.textLine(4, apOn ? "web:  " + WiFi.softAPIP().toString() : String(""), color::TIMER);
  ui.textLine(5, ntpSynced ? "cas:  z internetu" : browserTimeSet ? "cas:  z prohlizece" : "cas:  nenastaven", color::TEXT);
  ui.textLine(6, "klik zpet, drzet rezim", color::DIM);
}

void drawClock() {
  bool connected = WiFi.status() == WL_CONNECTED;
  ui.header("HODINY", color::CLOCK, connected ? "WiFi" : apOn ? "AP" : "...", connected ? color::STOPWATCH : color::TIMER);
  ui.progress(-1, color::CLOCK);

  struct tm t;
  if (!localNow(t)) {
    const int8_t dashes[4] = {DIGIT_DASH, DIGIT_DASH, DIGIT_DASH, DIGIT_DASH};
    const int8_t blank[2] = {DIGIT_BLANK, DIGIT_BLANK};
    ui.digits(dashes, true, blank, false, color::CLOCK);
    ui.subText("neznam cas - klikni", color::TEXT);
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

void draw(int64_t now) {
  switch (mode) {
    case Mode::Clock:
      if (showInfo) drawInfo();
      else drawClock();
      break;
    case Mode::Stopwatch: drawStopwatch(now); break;
    case Mode::Timer: drawTimer(now); break;
  }
}

// ---------------------------------------------------------------- sit

void onNtpSync(struct timeval *) { ntpSynced = true; }

void startSta(int64_t now) {
  staAttemptAt = now;
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.printf("[wifi] pripojuji se k '%s'\n", cfgSsid.c_str());
}

void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  // trvale hledani site by rusilo vlastni AP, proto dal zkousim jen obcas rucne
  WiFi.setAutoReconnect(false);
  apOn = true;
  Serial.printf("[wifi] AP '%s' zapnuto, web http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  WiFi.softAPdisconnect(true);
  apOn = false;
  Serial.println("[wifi] AP vypnuto");
}

void netBegin(int64_t now) {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  sntp_set_time_sync_notification_cb(onNtpSync);
  startSta(now);
}

void netLoop(int64_t now) {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !staWasConnected) {
    Serial.printf("[wifi] pripojeno, web http://%s\n", WiFi.localIP().toString().c_str());
    WiFi.setAutoReconnect(true);
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
    if (!mdnsStarted && MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
    }
  }
  if (!connected && staWasConnected) staAttemptAt = now;
  staWasConnected = connected;

  if (connected) {
    // AP nechavam, dokud je na nem nekdo pripojeny (treba prave nastavuje WiFi)
    if (apOn && WiFi.softAPgetStationNum() == 0) stopAp();
    return;
  }
  if (!apOn) {
    if (now - staAttemptAt > 20000) startAp();
  } else if (now - staAttemptAt > 60000 && WiFi.softAPgetStationNum() == 0) {
    startSta(now);
  }
}

// ---------------------------------------------------------------- web

String htmlEscape(const String &s) {
  String out;
  out.reserve(s.length());
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

void sendPage(const String &notice = "") {
  bool connected = WiFi.status() == WL_CONNECTED;
  String timeInfo = "není nastaven";
  struct tm t;
  if (localNow(t)) {
    char buf[40];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &t);
    timeInfo = String(buf) + (ntpSynced ? " (z internetu)" : " (z prohlížeče)");
  }
  String net = connected ? "připojeno k síti " + htmlEscape(cfgSsid) + ", adresa " + WiFi.localIP().toString()
                         : "nepřipojeno k síti " + htmlEscape(cfgSsid);
  uint32_t totalSec = countdown.totalMs / 1000;

  String page;
  page.reserve(3200);
  page += F("<!doctype html><html lang=cs><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'><title>Hodiny</title>"
            "<style>body{font-family:system-ui,sans-serif;max-width:26em;margin:1em auto;padding:0 1em;"
            "background:#111;color:#ddd}h1{color:#7df}h2{margin-top:1.6em;font-size:1.1em;color:#fb4}"
            "input,button{font-size:1em;padding:.45em;margin:.2em 0;border-radius:.3em;border:1px solid #555;"
            "background:#222;color:#eee}input[type=number]{width:4.5em}button{background:#246;cursor:pointer}"
            ".n{background:#243;padding:.6em;border-radius:.3em}label{display:block;margin-top:.4em}</style>"
            "<h1>Hodiny</h1>");
  if (!notice.isEmpty()) page += "<p class=n>" + notice + "</p>";
  page += "<p>Čas: <b>" + timeInfo + "</b></p>";
  if (!ntpSynced) page += F("<button onclick='st()'>Nastavit čas podle tohoto zařízení</button>");

  page += F("<h2>Minutka</h2><form method=post action=/timer>"
            "<input type=number name=m min=0 max=99 value=");
  page += totalSec / 60;
  page += F("> min <input type=number name=s min=0 max=59 value=");
  page += totalSec % 60;
  page += F("> s<br><button name=go value=0>Nastavit</button> "
            "<button name=go value=1>Nastavit a spustit</button></form>");

  page += "<h2>WiFi</h2><p>" + net + "</p>";
  page += F("<h2>Displej</h2><form method=post action=/rotate><button>Otočit o 180°</button></form>"
            "<script>function st(){fetch('/settime?t='+Math.floor(Date.now()/1000))"
            ".then(()=>location.replace('/'))}");
  // bez internetu si hodiny vezmou cas z prohlizece hned pri otevreni stranky
  if (!timeValid()) page += F("st();");
  page += F("</script>");
  server.send(200, "text/html; charset=utf-8", page);
}

void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetTime() {
  long long epoch = atoll(server.arg("t").c_str());
  if (epoch < 1700000000LL || epoch > 4102444800LL) {
    server.send(400, "text/plain", "neplatny cas");
    return;
  }
  if (!ntpSynced) {
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    browserTimeSet = true;
  }
  server.send(200, "text/plain", "ok");
}

void handleTimer() {
  long total = server.arg("m").toInt() * 60 + server.arg("s").toInt();
  total = constrain(total, 1L, (long)(TIMER_MAX_MS / 1000));
  stopAlarm();
  setTimerTotal(total * 1000UL);
  if (server.arg("go") == "1") countdown.toggle(nowMs());
  setMode(Mode::Timer);
  redirectHome();
}

void handleRotate() {
  rotated = !rotated;
  prefs.putBool("rot", rotated);
  ui.setRotated(rotated);
  redirectHome();
}

void webBegin() {
  server.on("/", HTTP_GET, []() { sendPage(); });
  server.on("/settime", HTTP_GET, handleSetTime);
  server.on("/timer", HTTP_POST, handleTimer);
  server.on("/rotate", HTTP_POST, handleRotate);
  server.onNotFound(redirectHome);
  server.begin();
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
  static const char *const MODES[] = {"hodiny", "stopky", "minutka"};
  struct tm t;
  char clock[20] = "nenastaven";
  if (localNow(t)) strftime(clock, sizeof(clock), "%H:%M:%S", &t);
  Serial.printf("[stav] beh=%llds reset=%d rezim=%s cas=%s stopky=%lld ms%s minutka=%lld/%lld ms%s wifi=%s ap=%d heap=%u\n",
                now / 1000, (int)esp_reset_reason(), MODES[(int)mode], clock, stopwatch.elapsed(now),
                stopwatch.running ? " (bezi)" : "", countdown.remaining(now), countdown.totalMs,
                alarmOn ? " (ALARM)" : countdown.running ? " (bezi)" : "",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-", apOn,
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
  webBegin();
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

  if (now - lastNetAt >= 1000) {
    lastNetAt = now;
    netLoop(now);
  }
  server.handleClient();

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
