# HODINY

Hodiny, stopky, minutka a hra River Raid pro modul **LAFVIN ESP32-C6 s 1,47" displejem** (ST7789, 172×320, bez dotyku).
Minutka je dělaná na kruhový trénink: krátké předvolby, poslední nastavený čas se pamatuje.

## Ovládání

Vše se ovládá jediným tlačítkem **BOOT**.

| Režim | Klik | Dvojklik |
|---|---|---|
| Minutka | start / pauza | v klidu další předvolba, jinak zrušení odpočtu |
| Stopky | start / stop | vynulování (jen když stojí) |
| Hodiny | stránka se stavem sítě | – |
| Hra | hra (ovládání viz níže) | simulace |
| Písmo | další písmo číslic: Latin → Klingon → Predator → Japan | – |

| Vypnout | uspí hodiny (displej, podsvícení i WiFi zhasnou) | zruší / zapne samočinné vypínání a přepne na hodiny |

- **Podržení** (0,7 s) přepíná režimy: hodiny → stopky → minutka → hra → písmo → vypnout.
- **Šetření baterie:** po 10 minutách bez stisku, když neběží minutka ani stopky, se hodiny uspí samy.
  Neplatí, jsou-li přes USB připojené k počítači. Probudí je stisk tlačítka a naběhnou jako po zapnutí.
- Zvolené písmo platí pro hodiny, stopky i minutku. Po resetu je vždy Latin.
- Předvolby minutky: 1:00, 1:15, 1:30, 1:45, 2:00, 5:00 (seznam je v `src/config.h`).
- Konec odpočtu hlásí blikání displeje a RGB LED (deska nemá bzučák), stisk alarm ukončí.
- Po zapnutí se zobrazí minutka. Po 30 s nečinnosti (stopky po 5 min stání) se displej vrátí na hodiny.

## Hra River Raid

Napodobenina hry z 8bitového Atari. Hraje se **na výšku**: displej se otočí, takže desku otoč o 90°.
Řeka ubíhá dolů, letadlo sestřeluje lodě (30 bodů), vrtulníky (60), stíhačky (100), nádrže (80)
a mosty (500). Palivo ubývá a doplňuje se průletem nad nádrží FUEL. Havárie je náraz do břehu,
ostrova, mostu nebo nepřítele a prázdná nádrž. Hráč má 4 letadla, každých 10 000 bodů jedno navíc.
Po havárii se pokračuje od posledního zničeného mostu.

Na úvodní obrazovce je volba: **klik = hra**, **dvojklik = simulace**.

**Simulace** hraje sama: autopilot řídí jako hráč s joystickem, plánuje trasu řekou asi 90 řádků
dopředu, vyhýbá se břehům a nepřátelům (předvídá, kam se pohnou), střílí po lodích, vrtulnících,
stíhačkách a mostech a při docházejícím palivu zamíří na nádrž, kterou pak nesestřelí.
Letí donekonečna; po havárii pokračuje od posledního mostu po nově vygenerované řece.
Klik simulaci ukončí, podržení přepne režim. Dokud simulace běží, hodiny se samy nevypnou.

Řeka se generuje tak, aby se dala vždy proletět: tvar se mění jen po dvojicích bloků a navazující
ramena mají vždy dost široký společný průjezd.

Ovládání hry jedním tlačítkem:

- **držet** = letadlo letí rovně a střílí,
- **pustit** = letadlo se dá do strany, pokaždé na opačnou stranu než minule
  (krátký stisk tedy otočí směr).

Za hry se režim nepřepíná; po konci hry klik = nová hra, dvojklik = simulace, podržení = další režim.
Nejlepší skóre se pamatuje i po vypnutí. Hra se kreslí v polovičním rozlišení (86×160 bodů,
každý bod 2×2 pixely), takže vypadá hrubě jako na Atari a stačí jí 27 kB paměti.

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
