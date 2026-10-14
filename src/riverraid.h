#pragma once
#include <Arduino.h>

class Arduino_GFX;
class Preferences;

// Hra River Raid podle Atari: reka ubiha dolu, letadlo sestreluje lode, vrtulniky, stihacky
// a mosty a nad nadrzemi FUEL doplnuje palivo. Hraje se na vysku.
// Ovladani jednim tlacitkem: drzet = letet rovne a strilet, pustit = uhnout do strany,
// pokazde na opacnou stranu nez minule. Simulace hraje sama autopilotem.
namespace riverraid {

void begin(Arduino_GFX *display, Preferences *prefs);
void enter();  // uvodni obrazovka; displej uz musi byt otoceny na vysku
void start(bool demo);  // nova hra, demo = simulace s autopilotem
void loop(int64_t now, bool held);  // held = tlacitko je prave stisknute

bool busy();  // hraje se nebo bezi simulace
bool demo();  // bezi simulace
// Klik a podrzeni mimo rucni hru (start, konec simulace, dalsi rezim). Tesne po konci hry
// se ignoruji, at stisk, kterym hrac jeste ridil letadlo, omylem nespusti dalsi hru.
bool acceptsMenuInput(int64_t pressedAt);

}  // namespace riverraid
