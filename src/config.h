#pragma once
#include <Arduino.h>

// Displej ST7789 172x320 na SPI
constexpr int8_t PIN_LCD_MOSI = 6;
constexpr int8_t PIN_LCD_SCLK = 7;
constexpr int8_t PIN_LCD_CS = 14;
constexpr int8_t PIN_LCD_DC = 15;
constexpr int8_t PIN_LCD_RST = 21;
constexpr int8_t PIN_LCD_BL = 22;

constexpr int8_t PIN_BUTTON = 9;  // tlacitko BOOT, aktivni v nule
constexpr int8_t PIN_LED = 8;  // adresovatelna RGB LED

constexpr uint8_t BACKLIGHT = 200;  // jas podsviceni 0-255

constexpr char HOSTNAME[] = "hodiny";

// Stredoevropsky cas vcetne letniho
constexpr char TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char NTP_SERVER_1[] = "tik.cesnet.cz";
constexpr char NTP_SERVER_2[] = "pool.ntp.org";

// WiFi se zapina jen na synchronizaci casu a pak se vypne (setri baterii)
constexpr uint32_t NTP_TIMEOUT_MS = 60 * 1000;            // jak dlouho na cas cekat
constexpr uint32_t NTP_RESYNC_MS = 6 * 60 * 60 * 1000;    // dalsi synchronizace po uspechu
constexpr uint32_t NTP_RETRY_MS = 30 * 60 * 1000;         // dalsi pokus po neuspechu

// Domaci WiFi, ze ktere si hodiny berou cas: WIFI_SSID a WIFI_PASS (vzor v secrets.example.h)
#include "secrets.h"

// Predvolby minutky v sekundach, prepina se dvojklikem; posledni nastaveny cas se pamatuje
constexpr uint16_t TIMER_PRESETS_SEC[] = {60, 75, 90, 105, 120, 300};
constexpr uint32_t TIMER_MAX_MS = (99 * 60 + 59) * 1000;

// Navrat na hodiny, kdyz se nic nedeje: minutka neodpocitava, stopky stoji
constexpr uint32_t TIMER_IDLE_RETURN_MS = 30 * 1000;
constexpr uint32_t STOPWATCH_IDLE_RETURN_MS = 5 * 60 * 1000;
constexpr uint32_t GAME_IDLE_RETURN_MS = 60 * 1000;  // hra, kdyz se nehraje

constexpr uint32_t ALARM_DURATION_MS = 60 * 1000;

// Setreni baterie: po teto dobe bez stisku (a kdyz nic nebezi) se hodiny uspi.
// Neplati, kdyz jsou pres USB pripojene k pocitaci.
constexpr uint32_t AUTO_OFF_MS = 10 * 60 * 1000;
