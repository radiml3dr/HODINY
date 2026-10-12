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
| Písmo | další písmo číslic: Latin → Klingon → Predator → Japan | – |

| Vypnout | uspí hodiny (displej, podsvícení i WiFi zhasnou) | – |

- **Podržení** (0,7 s) přepíná režimy: hodiny → stopky → minutka → písmo → vypnout.
- **Šetření baterie:** po 10 minutách bez stisku, když neběží minutka ani stopky, se hodiny uspí samy.
  Neplatí, jsou-li přes USB připojené k počítači. Probudí je stisk tlačítka a naběhnou jako po zapnutí.
- Zvolené písmo platí pro hodiny, stopky i minutku. Po resetu je vždy Latin.
- Předvolby minutky: 1:00, 1:15, 1:30, 1:45, 2:00, 5:00 (seznam je v `src/config.h`).
- Konec odpočtu hlásí blikání displeje a RGB LED (deska nemá bzučák), stisk alarm ukončí.
- Po zapnutí se zobrazí minutka. Po 30 s nečinnosti (stopky po 5 min stání) se displej vrátí na hodiny.

## Čas a síť

Deska nemá zálohované hodiny, čas si bere z internetu přes WiFi (středoevropský čas včetně letního).
Kvůli baterii se WiFi zapíná jen na synchronizaci: po startu a pak každých 6 hodin, nejdéle na minutu.
Když se čas získat nepodaří, zkusí to znovu za 30 minut.

## Sestavení

Projekt je pro [PlatformIO](https://platformio.org/).

1. Zkopíruj `src/secrets.example.h` jako `src/secrets.h` a doplň svou WiFi.
2. V `platformio.ini` uprav `upload_port` podle svého COM portu.
3. `pio run -t upload`

Platforma (pioarduino) a toolchain se instalují do `~/.pio-work/hodiny-core`, aby nepřepsaly balíčky
oficiální platformy `espressif32` v globálním `~/.platformio`.

Piny, předvolby a časy návratu na hodiny jsou v `src/config.h`.

## Písma

- **Klingon:** číslice pIqaD z fontu [Klingon pIqaD HaSta](https://www.evertype.com/fonts/tlh/)
  (Mike Neff, Michael Everson, licence SIL OFL 1.1).
- **Japan:** číslice kandži 〇一二三四五六七八九 z fontu
  [Noto Sans CJK JP](https://github.com/notofonts/noto-cjk) (Adobe, Google, licence SIL OFL 1.1).
- Bitmapy obou písem v `src/font_klingon.h` a `src/font_japan.h` generuje `tools/make_digit_font.py`.
- **Predator:** sedm čárek podle písma z odpočtu ve filmu; přiřazení čárek k číslicím je převzaté
  z fanouškovského fontu [Predator](https://www.fontspace.com/predator-font-f5510) (Giedi Prime,
  freeware pro nekomerční použití). Čárky kreslí přímo kód, font v projektu není.

## Ladění

Po sériové lince (115200 Bd) deska každých 10 s vypisuje stav a přijímá znaky
`k` (klik), `d` (dvojklik) a `p` (podržení).
