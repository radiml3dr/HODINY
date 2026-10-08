# HODINY

Hodiny, stopky a minutka pro modul **LAFVIN ESP32-C6 s 1,47" displejem** (ST7789, 172×320, bez dotyku).
Minutka je dělaná na kruhový trénink: krátké předvolby, poslední nastavený čas se pamatuje.

## Ovládání

Vše se ovládá jediným tlačítkem **BOOT**.

| Režim | Klik | Dvojklik |
|---|---|---|
| Minutka | start / pauza | v klidu další předvolba, jinak zrušení odpočtu |
| Stopky | start / stop | vynulování (jen když stojí) |
| Hodiny | stránka se stavem sítě | – |

- **Podržení** (0,7 s) přepíná režimy: hodiny → stopky → minutka.
- Předvolby minutky: 1:00, 1:15, 1:30, 1:45, 2:00, 5:00. Jiný čas jde nastavit z webové stránky.
- Konec odpočtu hlásí blikání displeje a RGB LED (deska nemá bzučák), stisk alarm ukončí.
- Po zapnutí se zobrazí minutka. Po 30 s nečinnosti (stopky po 5 min stání) se displej vrátí na hodiny.

## Čas a síť

Deska nemá zálohované hodiny, čas si bere z internetu přes WiFi (středoevropský čas včetně letního).
Když se k WiFi nepřipojí, zapne vlastní síť `Hodiny` (heslo `hodiny1234`); po otevření
`http://192.168.4.1` si převezme čas z telefonu.

Webová stránka (na adrese hodin v domácí síti) umí nastavit délku minutky a otočit displej o 180°.

## Sestavení

Projekt je pro [PlatformIO](https://platformio.org/).

1. Zkopíruj `src/secrets.example.h` jako `src/secrets.h` a doplň svou WiFi.
2. V `platformio.ini` uprav `upload_port` podle svého COM portu.
3. `pio run -t upload`

Platforma (pioarduino) a toolchain se instalují do `~/.pio-work/hodiny-core`, aby nepřepsaly balíčky
oficiální platformy `espressif32` v globálním `~/.platformio`.

Piny, předvolby a časy návratu na hodiny jsou v `src/config.h`.

## Ladění

Po sériové lince (115200 Bd) deska každých 10 s vypisuje stav a přijímá znaky
`k` (klik), `d` (dvojklik) a `p` (podržení).
