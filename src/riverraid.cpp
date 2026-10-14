#include "riverraid.h"

#include <Arduino_GFX_Library.h>
#include <Preferences.h>

#include "ui.h"

namespace riverraid {
namespace {

// Hra se kresli do ctvrtinoveho obrazku a na displej jde kazdy bod jako ctverecek 2x2:
// hrube body jako na Atari a v pameti jen 27 kB misto 110 kB.
constexpr int16_t SCALE = 2;
constexpr int16_t W = 172 / SCALE;
constexpr int16_t H = 320 / SCALE;
constexpr int16_t FIELD_H = 132;  // hraci plocha, pod ni panel se skore a palivem
constexpr int16_t MID = W / 2;    // reka je soumerna podle stredu jako na Atari
constexpr int16_t PLANE_ROW = 112;  // horni radek letadla na obrazovce
constexpr int16_t PUSH_ROWS = 8;    // kolik radku obrazku jde na displej najednou

constexpr uint32_t FRAME_MS = 33;
constexpr uint32_t DYING_MS = 1600;
constexpr uint32_t MENU_LOCK_MS = 1000;
constexpr uint32_t DEMO_HINT_MS = 3000;

// Svet: y roste nahoru (ulet vzdalenost), x je sloupec obrazku.
// Reka je slozena z bloku po 8 radcich; usek konci mostem, po zniceni mostu se po havarii
// zacina od dalsiho useku.
constexpr int16_t BLOCK_H = 8;
constexpr int32_t SECTION_BLOCKS = 28;
constexpr int32_t RING = 32;  // pamatovane bloky, vic nez se vejde na obrazovku
constexpr int8_t HW_MIN = 12, HW_MAX = 40, HW_START = 20, HW_BRIDGE = 12;
constexpr int8_t CHANNEL_MIN = 13;  // nejuzsi rameno vedle ostrova
// Letadlo je vysoke jako blok, takze skoro vzdy zasahuje do dvou bloku. Aby se reka dala
// vzdycky proletet, meni se tvar jen po dvojicich bloku a navazujici ramena maji spolecny
// kus vody aspon takhle siroky.
constexpr int8_t PASSAGE = 13;

constexpr float DRIFT_SPEED = 24;    // bodu za sekundu do strany
constexpr float BULLET_SPEED = 150;
constexpr float FUEL_BURN = 1.0f / 24;  // plna nadrz na 24 s
constexpr float FUEL_FILL = 0.4f;
constexpr uint8_t START_LIVES = 4;
constexpr uint32_t EXTRA_LIFE_EVERY = 10000;

// Autopilot simulace ridi jako hrac s joystickem: plynule doleva, doprava i rovne.
// Planuje trasu po krocich o par radku dopredu, kazdy sloupec ma cenu podle brehu a nepratel.
constexpr float AUTO_SPEED = 32;
constexpr int16_t AP_STEP = 3;     // radky mezi kroky planu
constexpr int16_t AP_STEPS = 30;   // plan na 90 radku dopredu
constexpr int16_t AP_RANGE = 110;  // jak daleko autopilot strili
constexpr int32_t AP_WALL = 1000000;

namespace pal {
constexpr uint16_t WATER = rgb(45, 50, 184);
constexpr uint16_t BANK = rgb(110, 156, 66);
constexpr uint16_t HUD = rgb(142, 142, 142);
constexpr uint16_t BLACK = rgb(0, 0, 0);
constexpr uint16_t WHITE = rgb(236, 236, 236);
constexpr uint16_t YELLOW = rgb(232, 232, 74);
constexpr uint16_t SCORE = rgb(252, 188, 116);
constexpr uint16_t ROAD = rgb(90, 90, 90);
constexpr uint16_t BRIDGE = rgb(181, 108, 40);
constexpr uint16_t BRIDGE_EDGE = rgb(120, 64, 20);
constexpr uint16_t HELI = rgb(24, 110, 86);
constexpr uint16_t RED = rgb(200, 72, 72);
constexpr uint16_t JET = rgb(84, 138, 210);
constexpr uint16_t ORANGE = rgb(252, 144, 72);
constexpr uint16_t ROOF = rgb(162, 52, 52);
}  // namespace pal

// ---------------------------------------------------------------- obrazky

struct Sprite {
  uint8_t w, h;
  const char *const *rows;
};

const char *const PLANE_ROWS[] = {
    "...Y...",
    "...Y...",
    "..YYY..",
    ".YYYYY.",
    "YYYYYYY",
    "Y.YYY.Y",
    "...Y...",
    ".YYYYY.",
};
// naklonene doleva, doprava je zrcadlove
const char *const PLANE_BANK_ROWS[] = {
    "...Y...",
    "...Y...",
    "..YYY..",
    ".YYYY..",
    "YYYYY..",
    "..YYY..",
    "...Y...",
    "..YYY..",
};
const char *const HELI_A_ROWS[] = {
    "KKKKKKK.",
    "....K...",
    "G..GGGG.",
    "GGGGGGGG",
    "G..GGGG.",
    "...K..K.",
};
const char *const HELI_B_ROWS[] = {
    ".KKKKKKK",
    "....K...",
    "G..GGGG.",
    "GGGGGGGG",
    "G..GGGG.",
    "...K..K.",
};
const char *const SHIP_ROWS[] = {
    ".......KK.......",
    "......WWWW......",
    "....KKKKKKKK....",
    "RRRRRRRRRRRRRRRR",
    ".KKKKKKKKKKKKKK.",
    "..KKKKKKKKKKKK..",
};
const char *const JET_ROWS[] = {
    "B.........",
    "BB...BB...",
    "BBBBBBBBBB",
    "BB..BBB...",
    "B.........",
};
const char *const HOUSE_ROWS[] = {
    "..RRRR..",
    ".RRRRRR.",
    "RRRRRRRR",
    ".WWWWWW.",
    ".WKWWWW.",
    ".WKWWWW.",
};
const char *const BOOM_A_ROWS[] = {
    "...O....",
    ".O..O.O.",
    "..OYYO..",
    "OOYYYYOO",
    "..OYYO..",
    ".O.O..O.",
    "....O...",
};
const char *const BOOM_B_ROWS[] = {
    "O..O..O.",
    "..O..O..",
    ".O.YY.O.",
    "O.YYYY.O",
    ".O.YY.O.",
    "..O..O..",
    "O..O..O.",
};

constexpr Sprite PLANE = {7, 8, PLANE_ROWS};
constexpr Sprite PLANE_BANK = {7, 8, PLANE_BANK_ROWS};
constexpr Sprite HELI_A = {8, 6, HELI_A_ROWS};
constexpr Sprite HELI_B = {8, 6, HELI_B_ROWS};
constexpr Sprite SHIP = {16, 6, SHIP_ROWS};
constexpr Sprite JET = {10, 5, JET_ROWS};
constexpr Sprite HOUSE = {8, 6, HOUSE_ROWS};
constexpr Sprite BOOM_A = {8, 7, BOOM_A_ROWS};
constexpr Sprite BOOM_B = {8, 7, BOOM_B_ROWS};

// Nadrz FUEL je cervena s pismeny pod sebou, pismena 3x3
constexpr int16_t FUEL_W = 7, FUEL_H = 16;
const char *const FUEL_LETTERS[4][3] = {
    {"###", "##.", "#.."},
    {"#.#", "#.#", "###"},
    {"###", "##.", "###"},
    {"#..", "#..", "###"},
};

uint16_t spriteColor(char c) {
  switch (c) {
    case 'Y': return pal::YELLOW;
    case 'K': return pal::BLACK;
    case 'G': return pal::HELI;
    case 'W': return pal::WHITE;
    case 'R': return pal::RED;
    case 'B': return pal::JET;
    case 'O': return pal::ORANGE;
    default: return pal::BLACK;
  }
}

// ---------------------------------------------------------------- stav hry

enum class State : uint8_t { Title, Ready, Playing, Dying, GameOver };
enum class Kind : uint8_t { Heli, Ship, Jet, Fuel, Boom };

constexpr uint8_t BRIDGE = 1, BRIDGE_DOWN = 2;

struct Block {
  int8_t hw;     // polovina sire reky
  int8_t iw;     // polovina sire ostrova uprostred, 0 = bez ostrova
  uint8_t flags;
  int8_t house;  // x domku na brehu, -1 = zadny
};

struct Obj {
  bool used;
  Kind kind;
  bool moving;
  int8_t dir;
  float x;
  int32_t wy;  // spodni radek
  float speed;
  int16_t trigger;  // na jakou vzdalenost od letadla se lod nebo vrtulnik rozjede
  float timer;      // vybuch
};

constexpr uint8_t MAX_OBJS = 24;

Arduino_GFX *out = nullptr;
Arduino_Canvas *canvas = nullptr;
uint16_t *fb = nullptr;
Preferences *store = nullptr;

State state = State::Title;
bool dirty = true;  // staticke obrazovky se kresli jen po zmene
int64_t lastFrameAt = 0;
int64_t stateAt = 0;
int64_t menuReadyAt = 0;
bool wasHeld = false;
bool demoMode = false;

uint32_t seed = 1;
uint32_t rng = 1;
Block ring[RING];
int32_t genNext = 0;     // dalsi blok ke generovani
int32_t firstBlock = 0;  // nejnizsi platny blok
int8_t genHw = HW_START, genIw = 0, islandLeft = 0, genCalm = 0;

Obj objs[MAX_OBJS];
float camY = 0;  // svetove y spodniho radku hraci plochy
float planeX = 0;
int8_t dir = 0;      // smer driftu, 0 = rovne
int8_t nextDir = 1;  // kam se poleti po pusteni tlacitka
bool bulletOn = false;
int16_t bulletX = 0;
float bulletY = 0;

float fuel = 1;
uint32_t score = 0;
uint32_t hiScore = 0;
uint8_t lives = START_LIVES;
int32_t checkpoint = 0;  // usek, od ktereho se zacina po havarii

uint32_t rnd(uint32_t n) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng % n;
}

int32_t sectionOf(int32_t block) { return block / SECTION_BLOCKS; }

float scrollSpeed() { return 26 + 2 * min<int32_t>(checkpoint, 9); }

// ---------------------------------------------------------------- reka

const Block &blockAt(int32_t wy) {
  static const Block WIDE = {HW_START, 0, 0, -1};
  int32_t k = wy >= 0 ? wy / BLOCK_H : -1;
  if (k < firstBlock || k >= genNext) return WIDE;
  return ring[k % RING];
}

// Okraje vody v ramenu, kde lezi x
void channel(const Block &b, int16_t x, int16_t &lo, int16_t &hi) {
  lo = MID - b.hw;
  hi = MID + b.hw;
  if (!b.iw) return;
  if (x < MID) hi = MID - b.iw;
  else lo = MID + b.iw;
}

bool isLand(const Block &b, int16_t x) {
  if (x < MID - b.hw || x >= MID + b.hw) return true;
  return b.iw && x >= MID - b.iw && x < MID + b.iw;
}

bool bridgeRow(const Block &b, int32_t wy) {
  int16_t o = wy % BLOCK_H;
  return (b.flags & BRIDGE) && o >= 2 && o <= 6;
}

Obj *newObj(Kind kind, float x, int32_t wy) {
  for (Obj &o : objs) {
    if (o.used) continue;
    o = {true, kind, false, 1, x, wy, 0, 0, 0};
    return &o;
  }
  return nullptr;
}

void spawnObjects(int32_t k, int32_t i, const Block &b) {
  if (i < 3 || i >= SECTION_BLOCKS - 4 || rnd(100) >= 45) return;
  int32_t section = sectionOf(k);
  uint32_t roll = rnd(100);
  int16_t side = b.iw ? (rnd(2) ? MID + b.iw : MID - b.iw - 1) : MID;
  int16_t lo, hi;
  channel(b, side, lo, hi);

  if (roll >= 80 && section > 0) {
    // stihacka leti pres celou obrazovku i nad brehy
    bool right = rnd(2);
    Obj *o = newObj(Kind::Jet, right ? -JET.w : W, k * BLOCK_H + 2);
    if (o) {
      o->dir = right ? 1 : -1;
      o->speed = 35 + 2 * min<int32_t>(section, 10);
      o->moving = true;
    }
    return;
  }

  if (roll >= 55 && i >= 4) {
    // nadrz je vysoka dva bloky, musi se vejit do vody v obou
    int16_t lo2, hi2;
    channel(ring[(k - 1) % RING], side, lo2, hi2);
    lo = max(lo, lo2);
    hi = min(hi, hi2);
    if (hi - lo < FUEL_W + 2) return;
    newObj(Kind::Fuel, lo + 1 + rnd(hi - lo - FUEL_W - 1), (k - 1) * BLOCK_H);
    return;
  }

  bool heli = roll < 30;
  int16_t w = heli ? HELI_A.w : SHIP.w;
  if (hi - lo < w + 2) return;
  Obj *o = newObj(heli ? Kind::Heli : Kind::Ship, lo + 1 + rnd(hi - lo - w - 1), k * BLOCK_H + 1);
  if (!o) return;
  o->dir = rnd(2) ? 1 : -1;
  o->speed = 8 + rnd(8) + min<int32_t>(section, 10);
  o->trigger = rnd(100) < 70 ? 30 + rnd(60) : -1000;  // nektere se nehnou vubec
}

// Da se z kazdeho ramene stare reky proletet do nektereho ramene nove?
bool passable(int8_t hw0, int8_t iw0, int8_t hw1, int8_t iw1) {
  Block a = {hw0, iw0, 0, -1}, b = {hw1, iw1, 0, -1};
  for (int16_t sa : {(int16_t)(MID - 1), MID}) {
    int16_t a0, a1;
    channel(a, sa, a0, a1);
    bool ok = false;
    for (int16_t sb : {(int16_t)(MID - 1), MID}) {
      int16_t b0, b1;
      channel(b, sb, b0, b1);
      ok |= min(a1, b1) - max(a0, b0) >= PASSAGE;
    }
    if (!ok) return false;
  }
  return true;
}

// Nahodna zmena tvaru reky na zacatku dvojice bloku
void reshape(int32_t i) {
  if (genCalm > 0) {
    genCalm--;
    return;
  }
  int8_t hw = genHw, iw = genIw;
  if (rnd(100) < 50) {
    int8_t step = (rnd(2) + 1) * 4;
    hw = constrain(hw + (rnd(2) ? step : -step), HW_MIN, HW_MAX);
  }
  if (iw) {
    // ostrov konci vcas pred mostem, at je pak cas se presunout doprostred
    if (--islandLeft <= 0 || i >= SECTION_BLOCKS - 10) iw = 0;
    else if (rnd(100) < 40) iw += rnd(2) ? 2 : -2;
  } else if (hw >= 28 && i >= 6 && i < SECTION_BLOCKS - 14 && rnd(100) < 25) {
    iw = 4 + rnd(hw - CHANNEL_MIN - 4 + 1);
    islandLeft = 2 + rnd(3);
  }
  if (iw && hw - iw < CHANNEL_MIN) iw = hw - CHANNEL_MIN;
  if (iw < 4) iw = 0;
  if (!passable(genHw, genIw, hw, iw)) return;
  if (genIw && !iw) genCalm = 1;  // po ostrove chvili klid, at se letadlo stihne presunout
  genHw = hw;
  genIw = iw;
}

// Kazdy usek se generuje ze sveho semene, takze po havarii vypada stejne jako poprve
void generate(int32_t k) {
  int32_t i = k % SECTION_BLOCKS;
  if (i == 0) {
    rng = seed ^ (uint32_t)(sectionOf(k) + 1) * 2654435761u;
    if (!rng) rng = 1;
    genHw = HW_START;
    genIw = 0;
    islandLeft = 0;
    genCalm = 0;
  }

  Block b = {HW_START, 0, 0, -1};
  if (i >= SECTION_BLOCKS - 4) {
    b.hw = HW_BRIDGE;  // zuzeni k mostu
    if (i == SECTION_BLOCKS - 1) b.flags = BRIDGE;
  } else if (i >= 4) {
    if (i % 2 == 0) reshape(i);
    b.hw = genHw;
    b.iw = genIw;
  }

  int16_t bank = MID - b.hw;
  if (!(b.flags & BRIDGE) && bank >= 12 && rnd(100) < 25) {
    int16_t x = 1 + rnd(bank - HOUSE.w - 2);
    b.house = rnd(2) ? x : W - 1 - x - HOUSE.w;
  }

  ring[k % RING] = b;
  spawnObjects(k, i, b);
}

void ensureGenerated() {
  int32_t top = (int32_t)camY + FIELD_H + BLOCK_H;
  while (genNext * BLOCK_H <= top) {
    generate(genNext++);
    if (genNext - firstBlock > RING) firstBlock = genNext - RING;
  }
}

// Novy svet od zacatku useku; letadlo uprostred reky, plna nadrz
void resetWorld(int32_t section) {
  for (Obj &o : objs) o.used = false;
  firstBlock = genNext = section * SECTION_BLOCKS;
  camY = genNext * BLOCK_H;
  ensureGenerated();
  planeX = MID - PLANE.w / 2;
  dir = 0;
  nextDir = 1;
  bulletOn = false;
  fuel = 1;
}

// ---------------------------------------------------------------- pravidla

int32_t planeBottom() { return (int32_t)camY + FIELD_H - 1 - (PLANE_ROW + PLANE.h - 1); }

int16_t spriteW(const Obj &o) {
  switch (o.kind) {
    case Kind::Heli: return HELI_A.w;
    case Kind::Ship: return SHIP.w;
    case Kind::Jet: return JET.w;
    case Kind::Fuel: return FUEL_W;
    default: return BOOM_A.w;
  }
}

int16_t spriteH(const Obj &o) {
  switch (o.kind) {
    case Kind::Heli: return HELI_A.h;
    case Kind::Ship: return SHIP.h;
    case Kind::Jet: return JET.h;
    case Kind::Fuel: return FUEL_H;
    default: return BOOM_A.h;
  }
}

void addScore(uint32_t pts) {
  uint32_t before = score;
  score += pts;
  if (score / EXTRA_LIFE_EVERY > before / EXTRA_LIFE_EVERY && lives < 9) lives++;
}

void explode(Obj &o) {
  float cx = o.x + spriteW(o) / 2.0f;
  int32_t cy = o.wy + spriteH(o) / 2;
  o.kind = Kind::Boom;
  o.x = cx - BOOM_A.w / 2.0f;
  o.wy = cy - BOOM_A.h / 2;
  o.timer = 0.5f;
}

void crash(int64_t now, const char *why) {
  state = State::Dying;
  stateAt = now;
  bulletOn = false;
  Serial.printf("[hra] havarie (%s), skore %u, zivoty %u\n", why, (unsigned)score, lives - 1);
}

void moveObjects(float dt, int32_t planeY) {
  for (Obj &o : objs) {
    if (!o.used) continue;
    if (o.wy + 20 < (int32_t)camY) {
      o.used = false;
      continue;
    }
    if (o.kind == Kind::Boom) {
      o.timer -= dt;
      if (o.timer <= 0) o.used = false;
      continue;
    }
    if (o.kind == Kind::Jet) {
      o.x += o.dir * o.speed * dt;
      if (o.dir > 0 && o.x > W) o.x = -JET.w;
      if (o.dir < 0 && o.x < -JET.w) o.x = W;
      continue;
    }
    if (o.kind == Kind::Fuel) continue;
    if (!o.moving && o.wy - planeY < o.trigger) o.moving = true;
    if (!o.moving) continue;
    int16_t w = spriteW(o);
    int16_t lo, hi;
    channel(blockAt(o.wy), o.x + w / 2, lo, hi);
    o.x += o.dir * o.speed * dt;
    if (o.x < lo) {
      o.x = lo;
      o.dir = 1;
    } else if (o.x + w > hi) {
      o.x = hi - w;
      o.dir = -1;
    }
  }
}

void moveBullet(float dt) {
  if (!bulletOn) return;
  float from = bulletY;
  bulletY += (BULLET_SPEED + scrollSpeed()) * dt;
  int32_t lo = (int32_t)from, hi = (int32_t)bulletY + 3;  // cela draha za snimek, at nic nepreleti

  for (Obj &o : objs) {
    if (!o.used || o.kind == Kind::Boom) continue;
    if (bulletX < o.x || bulletX >= o.x + spriteW(o)) continue;
    if (hi < o.wy || lo > o.wy + spriteH(o) - 1) continue;
    static const uint16_t POINTS[] = {60, 30, 100, 80};  // vrtulnik, lod, stihacka, nadrz
    addScore(POINTS[(uint8_t)o.kind]);
    explode(o);
    bulletOn = false;
    return;
  }

  for (int32_t wy = lo; wy <= hi; wy++) {
    const Block &b = blockAt(wy);
    if (!bridgeRow(b, wy) || (b.flags & BRIDGE_DOWN) || isLand(b, bulletX)) continue;
    ring[(wy / BLOCK_H) % RING].flags |= BRIDGE_DOWN;
    checkpoint = sectionOf(wy / BLOCK_H) + 1;
    addScore(500);
    Obj *boom = newObj(Kind::Fuel, MID - FUEL_W / 2, wy - 2);
    if (boom) explode(*boom);
    bulletOn = false;
    Serial.printf("[hra] most %d znicen\n", checkpoint);
    return;
  }

  if (bulletY > camY + FIELD_H) bulletOn = false;
}

// ---------------------------------------------------------------- autopilot

// Kde budou predmety, az kolem nich letadlo poleti: lod a vrtulnik zaberou celou drahu
// od soucasne polohy, protoze se mezitim muzou odrazit od brehu
struct ApObstacle {
  int32_t y0, y1;
  float x0, x1;    // predpovezena poloha vcetne rezervy
  float aim0, aim1;  // soucasna poloha, kam mirit
  bool fuel;
};
ApObstacle apObs[MAX_OBJS];
uint8_t apCount = 0;

void apPredict(int32_t planeY) {
  apCount = 0;
  float v = scrollSpeed();
  for (const Obj &o : objs) {
    if (!o.used || o.kind == Kind::Boom) continue;
    int16_t w = spriteW(o), h = spriteH(o);
    ApObstacle &a = apObs[apCount++];
    a.y0 = o.wy - 3;
    a.y1 = o.wy + h + 2;
    a.aim0 = o.x;
    a.aim1 = o.x + w;
    a.fuel = o.kind == Kind::Fuel;
    a.x0 = o.x;
    a.x1 = o.x + w;
    if (a.fuel || (!o.moving && o.trigger < 0)) continue;

    float reach = max(0.0f, (o.wy - planeY - PLANE.h) / v);  // kdy k nemu doleti spicka
    float pass = (h + PLANE.h + 6) / v;                      // jak dlouho jsou vedle sebe
    float move = o.dir * o.speed;
    if (o.kind == Kind::Jet) {
      // stihacka se po vyleteni z obrazovky vraci z druhe strany
      float span = W + JET.w;
      float start = fmodf(o.x + JET.w + move * reach, span);
      if (start < 0) start += span;
      start -= JET.w;
      float end = start + move * pass;
      a.x0 = min(start, end);
      a.x1 = max(start, end) + w;
    } else {
      float far = o.x + move * (reach + pass);
      a.x0 = min(o.x, far);
      a.x1 = max(o.x, far) + w;
    }
  }
}

// Cena polohy letadla s levym okrajem x v radku wy: breh nejde, blizko brehu a nepritel
// je drahy, nadrz pri nedostatku paliva a mireni na nepritele zdola zlevnuji
int32_t apCell(int32_t wy, int16_t x) {
  // letadlo je vysoke 8 radku, takze muze zasahovat do dvou bloku
  const Block &b = blockAt(wy);
  const Block &nose = blockAt(wy + PLANE.h - 1);
  int16_t right = x + PLANE.w - 1;
  if (isLand(b, x) || isLand(b, right) || isLand(nose, x) || isLand(nose, right)) return AP_WALL;
  int16_t lo, hi, lo2, hi2;
  channel(b, x + PLANE.w / 2, lo, hi);
  channel(nose, x + PLANE.w / 2, lo2, hi2);
  int16_t margin = min<int16_t>(x - max(lo, lo2), min(hi, hi2) - 1 - right);
  int32_t cost = margin < 6 ? (6 - margin) * 6 : 0;
  int16_t gun = x + PLANE.w / 2;
  for (uint8_t i = 0; i < apCount; i++) {
    const ApObstacle &a = apObs[i];
    bool overlapX = right >= a.x0 - 2 && x < a.x1 + 2;
    if (wy >= a.y0 && wy <= a.y1) {
      if (a.fuel) {
        if (overlapX && fuel < 0.7f) cost -= fuel < 0.35f ? 150 : 40;  // skoro prazdna nadrz ma prednost
      } else if (overlapX) {
        cost += 400;
      }
    } else if (!a.fuel && wy < a.y0 - 22 && wy > a.y0 - 57 && gun >= a.aim0 && gun < a.aim1) {
      cost -= 3;  // pod nepritelem se vyplati zamirit
    }
  }
  return cost;
}

// Kam se posunout: zpetny pruchod planem od nejvzdalenejsiho radku k letadlu
int16_t apTarget(int16_t px, int32_t planeY) {
  constexpr int16_t COLS = W - PLANE.w + 1;
  static int32_t next[COLS], cur[COLS];
  int16_t shift = max<int16_t>(1, (int16_t)(AUTO_SPEED * AP_STEP / scrollSpeed()));
  apPredict(planeY);
  for (int16_t x = 0; x < COLS; x++) next[x] = 0;
  for (int16_t k = AP_STEPS - 1; k >= 1; k--) {
    int32_t wy = planeY + k * AP_STEP;
    for (int16_t x = 0; x < COLS; x++) {
      int32_t best = AP_WALL;
      for (int16_t nx = max<int16_t>(0, x - shift); nx <= min<int16_t>(COLS - 1, x + shift); nx++) {
        best = min(best, next[nx]);
      }
      int32_t c = apCell(wy, x);
      cur[x] = c >= AP_WALL || best >= AP_WALL ? AP_WALL : c + best;
    }
    memcpy(next, cur, sizeof(next));
  }
  // nejlevnejsi dosazitelny sloupec, pri shode co nejblize
  int16_t target = px;
  int32_t best = AP_WALL + 1;
  for (int16_t d = 0; d <= shift; d++) {
    const int16_t candidates[2] = {(int16_t)(px - d), (int16_t)(px + d)};
    for (int16_t nx : candidates) {
      if (nx < 0 || nx >= COLS || next[nx] >= best) continue;
      best = next[nx];
      target = nx;
    }
  }
  return target;
}

// Strili na nejblizsi cil nad sebou; nadrz setri, kdyz potrebuje palivo
bool apFire(int16_t px, int32_t planeY) {
  int16_t gun = px + PLANE.w / 2;
  int32_t from = planeY + PLANE.h, to = planeY + AP_RANGE;
  const Obj *near = nullptr;
  for (const Obj &o : objs) {
    if (!o.used || o.kind == Kind::Boom || gun < o.x || gun >= o.x + spriteW(o)) continue;
    if (o.wy < from || o.wy > to || (near && near->wy <= o.wy)) continue;
    near = &o;
  }
  for (int32_t wy = from; wy <= to; wy++) {
    if (near && wy >= near->wy) break;
    const Block &b = blockAt(wy);
    if (bridgeRow(b, wy) && !(b.flags & BRIDGE_DOWN) && !isLand(b, gun)) return true;
  }
  return near && (near->kind != Kind::Fuel || fuel > 0.75f);
}

bool autopilot(float dt) {
  int16_t px = (int16_t)planeX;
  int32_t planeY = planeBottom();
  int16_t target = apTarget(px, planeY);
  float step = AUTO_SPEED * dt;
  if (target > px) {
    planeX = min(planeX + step, (float)target);
    dir = 1;
  } else if (target < px) {
    planeX = max(planeX - step, (float)target);
    dir = -1;
  } else {
    dir = 0;
  }
  return apFire(px, planeY);
}

// ---------------------------------------------------------------- hra

void updatePlaying(int64_t now, float dt, bool held) {
  bool fire = held;
  if (demoMode) {
    fire = autopilot(dt);
  } else {
    if (held && !wasHeld) {
      if (dir) nextDir = -dir;
      dir = 0;
    } else if (!held && wasHeld) {
      dir = nextDir;
    }
    planeX += dir * DRIFT_SPEED * dt;
  }
  if (fire && !bulletOn) {
    bulletOn = true;
    bulletX = (int16_t)planeX + PLANE.w / 2;
    bulletY = planeBottom() + PLANE.h;
  }

  camY += scrollSpeed() * dt;
  ensureGenerated();
  int32_t planeY = planeBottom();
  moveObjects(dt, planeY);
  moveBullet(dt);

  fuel -= FUEL_BURN * dt;
  int16_t px = (int16_t)planeX;
  for (Obj &o : objs) {
    if (!o.used || o.kind == Kind::Boom) continue;
    int16_t w = spriteW(o), h = spriteH(o);
    if (px + PLANE.w - 1 <= o.x || px + 1 >= o.x + w) continue;
    if (planeY + PLANE.h - 1 <= o.wy || planeY + 1 >= o.wy + h) continue;
    if (o.kind == Kind::Fuel) {
      fuel = min(1.0f, fuel + (FUEL_FILL + FUEL_BURN) * dt);
    } else {
      static const char *const WHO[] = {"vrtulnik", "lod", "stihacka"};
      crash(now, WHO[(uint8_t)o.kind]);
      explode(o);
      return;
    }
  }
  if (fuel <= 0) {
    fuel = 0;
    crash(now, "palivo");
    return;
  }

  // breh, ostrov a most podle skutecnych bodu letadla
  const Sprite &s = dir ? PLANE_BANK : PLANE;
  for (int16_t j = 0; j < s.h; j++) {
    int32_t wy = planeY + s.h - 1 - j;
    const Block &b = blockAt(wy);
    if (bridgeRow(b, wy) && !(b.flags & BRIDGE_DOWN)) {
      crash(now, "most");
      return;
    }
    int16_t first = -1, last = -1;
    for (int16_t i = 0; i < s.w; i++) {
      if (s.rows[j][i] == '.') continue;
      int16_t c = dir > 0 ? s.w - 1 - i : i;
      if (first < 0 || c < first) first = c;
      if (c > last) last = c;
    }
    if (first >= 0 && (isLand(b, px + first) || isLand(b, px + last))) {
      crash(now, "breh");
      return;
    }
  }
}

void gameOver(int64_t now) {
  state = State::GameOver;
  menuReadyAt = now + MENU_LOCK_MS;
  if (score > hiScore) {
    hiScore = score;
    store->putUInt("rrhi", hiScore);
  }
  Serial.printf("[hra] konec, skore %u, rekord %u\n", (unsigned)score, (unsigned)hiScore);
}

// ---------------------------------------------------------------- kresleni

void fill(int16_t x0, int16_t x1, int16_t y, uint16_t col) {
  if (y < 0 || y >= H) return;
  x0 = max<int16_t>(x0, 0);
  x1 = min<int16_t>(x1, W);
  uint16_t *p = fb + y * W;
  for (int16_t x = x0; x < x1; x++) p[x] = col;
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col) {
  for (int16_t j = 0; j < h; j++) fill(x, x + w, y + j, col);
}

void pixel(int16_t x, int16_t y, uint16_t col) {
  if (x >= 0 && x < W && y >= 0 && y < FIELD_H) fb[y * W + x] = col;
}

int16_t screenRow(int32_t wy) { return FIELD_H - 1 - (wy - (int32_t)camY); }

void drawSprite(const Sprite &s, int16_t x, int16_t top, bool flip) {
  for (int16_t j = 0; j < s.h; j++) {
    for (int16_t i = 0; i < s.w; i++) {
      char c = s.rows[j][flip ? s.w - 1 - i : i];
      if (c != '.') pixel(x + i, top + j, spriteColor(c));
    }
  }
}

void drawFuel(int16_t x, int16_t top) {
  for (int16_t j = 0; j < FUEL_H; j++) {
    for (int16_t i = 0; i < FUEL_W; i++) pixel(x + i, top + j, pal::RED);
  }
  for (int16_t l = 0; l < 4; l++) {
    for (int16_t j = 0; j < 3; j++) {
      for (int16_t i = 0; i < 3; i++) {
        if (FUEL_LETTERS[l][j][i] == '#') pixel(x + 2 + i, top + l * 4 + 1 + j, pal::WHITE);
      }
    }
  }
}

void drawRiver() {
  for (int16_t r = 0; r < FIELD_H; r++) {
    int32_t wy = (int32_t)camY + FIELD_H - 1 - r;
    const Block &b = blockAt(wy);
    int16_t lo = MID - b.hw, hi = MID + b.hw;
    fill(0, W, r, pal::BANK);
    fill(lo, hi, r, pal::WATER);
    if (b.iw) fill(MID - b.iw, MID + b.iw, r, pal::BANK);
    if (!bridgeRow(b, wy)) continue;

    // silnice pres breh a most pres vodu
    int16_t o = wy % BLOCK_H;
    fill(0, lo, r, pal::ROAD);
    fill(hi, W, r, pal::ROAD);
    if (o == 4) {
      for (int16_t x = 1; x < W; x += 4) {
        if (x + 2 <= lo || x >= hi) fill(x, x + 2, r, pal::YELLOW);
      }
    }
    if (!(b.flags & BRIDGE_DOWN)) fill(lo, hi, r, o == 2 || o == 6 ? pal::BRIDGE_EDGE : pal::BRIDGE);
  }

  int32_t k0 = max<int32_t>((int32_t)camY / BLOCK_H, firstBlock);
  int32_t k1 = min<int32_t>(((int32_t)camY + FIELD_H) / BLOCK_H, genNext - 1);
  for (int32_t k = k0; k <= k1; k++) {
    const Block &b = ring[k % RING];
    if (b.house >= 0) drawSprite(HOUSE, b.house, screenRow(k * BLOCK_H + 1) - HOUSE.h + 1, false);
  }
}

void drawObjects(int64_t now) {
  bool rotor = (now / 80) % 2;
  for (const Obj &o : objs) {
    if (!o.used) continue;
    int16_t x = (int16_t)floorf(o.x);
    int16_t top = screenRow(o.wy) - spriteH(o) + 1;
    switch (o.kind) {
      case Kind::Heli: drawSprite(rotor ? HELI_A : HELI_B, x, top, o.dir < 0); break;
      case Kind::Ship: drawSprite(SHIP, x, top, false); break;
      case Kind::Jet: drawSprite(JET, x, top, o.dir < 0); break;
      case Kind::Fuel: drawFuel(x, top); break;
      case Kind::Boom: drawSprite(o.timer > 0.25f ? BOOM_A : BOOM_B, x, top, false); break;
    }
  }
}

void drawPlane(int64_t now) {
  int16_t x = (int16_t)planeX;
  if (state == State::Dying) {
    bool a = ((now - stateAt) / 120) % 2;
    drawSprite(a ? BOOM_A : BOOM_B, x - 1, PLANE_ROW, false);
    return;
  }
  drawSprite(dir ? PLANE_BANK : PLANE, x, PLANE_ROW, dir > 0);
  if (bulletOn) {
    int16_t top = screenRow((int32_t)bulletY + 3);
    for (int16_t j = 0; j < 4; j++) pixel(bulletX, top + j, pal::YELLOW);
  }
}

void text(int16_t x, int16_t y, const String &s, uint16_t col) {
  canvas->setTextColor(col);
  canvas->setCursor(x, y);
  canvas->print(s);
}

void textCenter(int16_t y, const String &s, uint16_t col) { text((W - (int16_t)s.length() * 6) / 2 + 1, y, s, col); }

void drawHud(int64_t now) {
  rect(0, FIELD_H, W, H - FIELD_H, pal::HUD);
  fill(0, W, FIELD_H, pal::BLACK);
  textCenter(FIELD_H + 4, String(score), pal::SCORE);

  // ukazatel paliva E 1/2 F
  constexpr int16_t GX = 23, GY = FIELD_H + 14, GW = 40, GH = 11;
  rect(GX, GY, GW, GH, pal::BLACK);
  text(GX + 2, GY + 2, "E", pal::WHITE);
  text(GX + GW - 7, GY + 2, "F", pal::WHITE);
  rect(GX + GW / 2, GY + 2, 1, 4, pal::WHITE);
  bool low = fuel < 0.25f;
  if (!low || (now / 250) % 2) {
    int16_t nx = GX + 2 + (int16_t)(fuel * (GW - 6));
    rect(nx, GY + 1, 2, GH - 2, low ? pal::RED : pal::ORANGE);
  }

  // nahradni letadla, simulace ma nekonecne
  if (!demoMode && lives > 1) text(10, GY + 2, String(lives - 1), pal::YELLOW);
}

void drawBox(int16_t y, int16_t rows) {
  rect(4, y - 4, W - 8, rows * 11 + 6, pal::BLACK);
}

void drawMenu(int16_t y) {
  textCenter(y, "klik = hra", pal::SCORE);
  textCenter(y + 11, "2x = simulace", pal::SCORE);
  textCenter(y + 22, "drzet = rezim", pal::HUD);
}

void drawOverlay(int64_t now) {
  switch (state) {
    case State::Title:
      drawBox(20, 6);
      textCenter(20, "RIVER RAID", pal::YELLOW);
      textCenter(36, "rekord " + String(hiScore), pal::WHITE);
      drawMenu(53);
      break;
    case State::Ready:
      drawBox(30, 3);
      textCenter(30, "stiskni", pal::YELLOW);
      textCenter(46, "drz = strilej", pal::WHITE);
      textCenter(57, "pust = zatoc", pal::WHITE);
      break;
    case State::Playing:
      if (demoMode && now - stateAt < DEMO_HINT_MS) {
        drawBox(30, 2);
        textCenter(30, "SIMULACE", pal::YELLOW);
        textCenter(41, "klik = konec", pal::WHITE);
      }
      break;
    case State::GameOver:
      drawBox(20, 6);
      textCenter(20, "KONEC HRY", pal::YELLOW);
      textCenter(36, score >= hiScore && score ? String("NOVY REKORD") : "rekord " + String(hiScore), pal::WHITE);
      drawMenu(53);
      break;
    default: break;
  }
}

// Obrazek 86x160 jde na displej zvetseny na 172x320 po pruzich
void push() {
  static uint16_t strip[W * SCALE * PUSH_ROWS * SCALE];
  for (int16_t y0 = 0; y0 < H; y0 += PUSH_ROWS) {
    uint16_t *dst = strip;
    for (int16_t y = y0; y < y0 + PUSH_ROWS; y++) {
      const uint16_t *src = fb + y * W;
      for (int16_t x = 0; x < W; x++) dst[2 * x] = dst[2 * x + 1] = src[x];
      memcpy(dst + W * SCALE, dst, W * SCALE * sizeof(uint16_t));
      dst += W * SCALE * SCALE;
    }
    out->draw16bitRGBBitmap(0, y0 * SCALE, strip, W * SCALE, PUSH_ROWS * SCALE);
  }
}

void render(int64_t now) {
  drawRiver();
  drawObjects(now);
  if (state != State::Title) drawPlane(now);
  drawHud(now);
  drawOverlay(now);
  push();
}

void setState(State s, int64_t now) {
  state = s;
  stateAt = now;
  dirty = true;
}

}  // namespace

void begin(Arduino_GFX *display, Preferences *prefs) {
  out = display;
  store = prefs;
  hiScore = store->getUInt("rrhi", 0);
  canvas = new Arduino_Canvas(W, H, nullptr);
  canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  canvas->setTextSize(1);
  canvas->setTextWrap(false);
  fb = canvas->getFramebuffer();
}

void enter() {
  demoMode = false;
  seed = esp_random();
  score = 0;
  checkpoint = 0;
  resetWorld(0);
  setState(State::Title, 0);
}

void start(bool demo) {
  demoMode = demo;
  seed = esp_random();
  score = 0;
  lives = START_LIVES;
  checkpoint = 0;
  resetWorld(0);
  wasHeld = false;
  // simulace leti hned, hra ceka na prvni stisk
  setState(demo ? State::Playing : State::Ready, esp_timer_get_time() / 1000);
  Serial.println(demo ? "[hra] simulace" : "[hra] start");
}

bool busy() { return state == State::Ready || state == State::Playing || state == State::Dying; }

bool demo() { return demoMode && busy(); }

bool acceptsMenuInput(int64_t pressedAt) { return (demoMode || !busy()) && pressedAt >= menuReadyAt; }

void loop(int64_t now, bool held) {
  if (!fb || now - lastFrameAt < FRAME_MS) return;
  float dt = min<int64_t>(now - lastFrameAt, 50) / 1000.0f;
  lastFrameAt = now;

  switch (state) {
    case State::Ready:
      if (held && !wasHeld) {
        // stisk, ktery hru spustil, uz zaroven leti rovne a strili
        setState(State::Playing, now);
        updatePlaying(now, dt, held);
      }
      break;
    case State::Playing:
      updatePlaying(now, dt, held);
      break;
    case State::Dying:
      if (now - stateAt >= DYING_MS) {
        if (demoMode) {
          // simulace leti dal od posledniho mostu, ale po jine rece, at nenabouri na stejnem miste
          seed = esp_random();
          resetWorld(checkpoint);
          setState(State::Playing, now);
        } else if (--lives == 0) {
          gameOver(now);
          setState(State::GameOver, now);
        } else {
          resetWorld(checkpoint);
          setState(State::Ready, now);
        }
      }
      break;
    default: break;
  }
  if (state == State::Playing || state == State::Dying) {
    if (state == State::Dying) {
      // vybuchy dohori, svet stoji
      for (Obj &o : objs) {
        if (o.used && o.kind == Kind::Boom && (o.timer -= dt) <= 0) o.used = false;
      }
    }
    dirty = true;
  }
  wasHeld = held;

  if (!dirty) return;
  dirty = false;
  render(now);
}

}  // namespace riverraid
