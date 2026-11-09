// Host-Test: Parser (synthetische Frames, Muell, Sync-Verlust) + Fusion (bewegt -> stehend) + Frontfilter.
// Konfigurationen (run_host_test.sh): Standard (2 Sensoren), -DENABLE_LD2420=1, -DSHOW_RANGE_ONLY_RINGS=1.
// Bauen/Starten: siehe test/run_host_test.sh
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <chrono>
#include "../src/ld_parsers.h"
#include "../src/fusion.h"
#include "../src/sim.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

template <class P> int feedAll(P& p, const std::vector<uint8_t>& v) { int n = 0; for (uint8_t b : v) n += p.feed(b); return n; }
static std::vector<uint8_t> vec(const uint8_t* p, int n) { return std::vector<uint8_t>(p, p + n); }
static void cat(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }
__attribute__((unused)) static std::vector<uint8_t> str(const char* s) { return std::vector<uint8_t>(s, s + strlen(s)) ; }

static void test_ld2450() {
  printf("[LD2450 Parser]\n");
  uint8_t f[30];
  int16_t t1[3][3] = {{-1200, 1500, -35}, {800, 2500, 0}, {0, 0, 0}};
  encode2450(f, t1, 2);
  Ld2450Parser p;
  CHECK(feedAll(p, vec(f, 30)) == 1);
  CHECK(p.last.t[0].valid && p.last.t[0].x == -1200 && p.last.t[0].y == 1500 && p.last.t[0].speed == -35);
  CHECK(p.last.t[1].valid && p.last.t[1].x == 800 && p.last.t[1].y == 2500 && p.last.t[1].speed == 0);
  CHECK(!p.last.t[2].valid);
  // Bytes laut Spezifikation von Hand: x=-1200 -> 0x04B0 ohne Bit15 -> B0 04
  CHECK(f[4] == 0xB0 && f[5] == 0x04);
  CHECK(f[6] == (uint8_t)(1500 & 255) && f[7] == (uint8_t)(0x80 | (1500 >> 8)));
  // Muell davor + Header-Fragment + abgeschnittener Frame + guter Frame (Sync-Verlust)
  std::vector<uint8_t> s = {0x00, 0xAA, 0xFF, 0x13, 0x55, 0xCC, 0xAA, 0xFF, 0x03};
  cat(s, vec(f, 17));            // abgeschnittener Frame
  cat(s, vec(f, 30));            // danach kompletter Frame
  Ld2450Parser q;
  int n = feedAll(q, s);
  CHECK(n == 1);
  CHECK(q.last.t[0].x == -1200);
  // Frame mit falschem Footer wird verworfen, der naechste gute kommt durch
  uint8_t bad[30]; memcpy(bad, f, 30); bad[29] = 0xCD;
  std::vector<uint8_t> s2 = vec(bad, 30); cat(s2, vec(f, 30));
  Ld2450Parser r; CHECK(feedAll(r, s2) == 1);
  // zwei Frames direkt hintereinander, byteweise
  std::vector<uint8_t> s3 = vec(f, 30); cat(s3, vec(f, 30));
  Ld2450Parser w; CHECK(feedAll(w, s3) == 2);
  // Fuzz: 200k Zufallsbytes duerfen nicht abstuerzen
  Ld2450Parser z; srand(1);
  for (int i = 0; i < 200000; i++) z.feed((uint8_t)rand());
  CHECK(z.bytes == 200000);
  // nach Fuzz sofort wieder synchron
  CHECK(feedAll(z, vec(f, 30)) >= 1);
}

static void test_ld2410() {
  printf("[LD2410C Parser]\n");
  uint8_t f[23];
  int n = encode2410(f, 3, 250, 60, 245, 45, 250);
  CHECK(n == 23);
  Ld2410Parser p;
  CHECK(feedAll(p, vec(f, 23)) == 1);
  CHECK(p.last.state == 3 && p.last.moveDist == 250 && p.last.moveEnergy == 60 &&
        p.last.stillDist == 245 && p.last.stillEnergy == 45 && p.last.detDist == 250 && !p.last.engineering);
  // Muell + zerbrochener Frame + guter
  std::vector<uint8_t> s = {0xF4, 0xF3, 0x00, 0xF1, 0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF};  // Laenge unplausibel
  cat(s, vec(f, 10)); cat(s, vec(f, 23));
  Ld2410Parser q; CHECK(feedAll(q, s) == 1);
  // Engineering-Frame: Payload 35 Byte (Typ 01), danach Normalframe
  uint8_t e[4 + 2 + 35 + 4] = {0xF4, 0xF3, 0xF2, 0xF1, 35, 0};
  uint8_t* pl = e + 6; memset(pl, 0, 35);
  pl[0] = 0x01; pl[1] = 0xAA; pl[2] = 0x02; pl[6] = 100; pl[7] = 0; pl[8] = 70; pl[33] = 0x55; pl[34] = 0x00;
  e[41] = 0xF8; e[42] = 0xF7; e[43] = 0xF6; e[44] = 0xF5;
  Ld2410Parser r; std::vector<uint8_t> s2 = vec(e, 45); cat(s2, vec(f, 23));
  CHECK(feedAll(r, s2) == 2);
  CHECK(r.last.engineering == false);       // zuletzt kam der Normalframe
  Ld2410Parser r2; CHECK(feedAll(r2, vec(e, 45)) == 1 && r2.last.engineering && r2.last.stillDist == 100 && r2.last.state == 2);
  // Footer kaputt
  uint8_t bad[23]; memcpy(bad, f, 23); bad[22] = 0x00;
  Ld2410Parser b; CHECK(feedAll(b, vec(bad, 23)) == 0);
  CHECK(feedAll(b, vec(f, 23)) == 1);
  Ld2410Parser z; srand(2);
  for (int i = 0; i < 200000; i++) z.feed((uint8_t)rand());
  CHECK(feedAll(z, vec(f, 23)) >= 1);
}

static void test_ld2420() {
#if ENABLE_LD2420
  printf("[LD2420 Parser]\n");
  Ld2420Parser p;
  std::vector<uint8_t> s = {0xE0, 0x9F, 0x00, 0xFF};   // Boot-Muell
  cat(s, str("ON\r\nRange 123\r\nOFF\r\n"));
  int n = feedAll(p, s);
  CHECK(n == 3);
  CHECK(p.last.presence == false && p.last.rangeValid && p.last.range == 123);
  Ld2420Parser q; feedAll(q, str("ON\r\nRange 60\r\n"));
  CHECK(q.last.presence && q.last.range == 60);
  // zerhackte Zeile + Zeilen mit Unsinn
  Ld2420Parser r; feedAll(r, str("ON\r\nRan\r\nRange abc\r\nRange 99999999\r\nRange 75\r\n"));
  CHECK(r.last.range == 75 && r.last.presence);
  // sehr lange Zeile ohne Zeilenende ueberlaeuft nicht
  Ld2420Parser l; std::string longs(500, 'x'); feedAll(l, str(longs.c_str())); CHECK(feedAll(l, str("\r\nON\r\n")) == 1);
  // Energy-Frame (optional, ungetestet gegen Hardware)
  uint8_t e[45] = {0xF4, 0xF3, 0xF2, 0xF1, 35, 0}; memset(e + 6, 0, 35);
  e[6] = 1; e[7] = 0x2C; e[8] = 0x01; e[41] = 0xF8; e[42] = 0xF7; e[43] = 0xF6; e[44] = 0xF5;
  Ld2420Parser en; CHECK(feedAll(en, vec(e, 45)) == 1 && en.last.presence && en.last.range == 300 && en.last.fromEnergyFrame);
  CHECK(feedAll(en, str("OFF\r\n")) == 1 && !en.last.presence);
  Ld2420Parser z; srand(3);
  for (int i = 0; i < 200000; i++) z.feed((uint8_t)rand());
  CHECK(feedAll(z, str("\r\nON\r\n")) >= 1);
#else
  printf("[LD2420] uebersprungen (ENABLE_LD2420=0)\n");
#endif
}

// ---------- Fusion ----------
struct Rig {
  Ld2450Parser a; Ld2410Parser b;
#if ENABLE_LD2420
  Ld2420Parser c;
#endif
  Tracker trk; uint32_t now = 0;
  void feedSim(uint32_t tms, bool send450 = true, bool send410 = true, bool send420 = true) {
    SimBytes sb; simStep(tms, sb);
    if (send450) for (int i = 0; i < sb.n2450; i++) if (a.feed(sb.b2450[i])) trk.onFrame2450(a.last, now);
    if (send410) for (int i = 0; i < sb.n2410; i++) if (b.feed(sb.b2410[i])) trk.onReport2410(b.last, now);
#if ENABLE_LD2420
    if (send420) for (int i = 0; i < sb.n2420; i++) if (c.feed((uint8_t)sb.b2420[i])) trk.onReport2420(c.last, now);
#else
    (void)send420;
#endif
  }
  // simuliert Zeit bis t (ms) in 20-ms-Ticks mit 100-ms-Sensorschritten
  void runTo(uint32_t t, bool s450 = true, bool s410 = true, bool s420 = true) {
    while (now < t) {
      now += 20;
      if (now % 100 == 0) feedSim(now, s450, s410, s420);
      trk.tick(now);
    }
  }
  const Track* byId(int id) const { for (auto& t : trk.tr) if (t.used && t.id == id) return &t; return nullptr; }
  const Track* any() const { for (auto& t : trk.tr) if (t.used) return &t; return nullptr; }
};

static void test_fusion() {
  printf("[Fusion Szenario: A laeuft, steht (LD2450 verliert), laeuft weiter; B laeuft durch]\n");
  Rig r; int idA = 0;
  r.runTo(3000);
  const Track* a = r.any();
  CHECK(a && r.trk.count() == 1);
  if (a) { idA = a->id; CHECK(a->st == TS_MOVING); CHECK(a->confirmed); CHECK(a->agreeBy[S2450] && a->agree >= 2); }
  printf("  t=3.0s : Ziel %d bewegt, bestaetigt=%d (Sensoren %d)\n", idA, a ? a->confirmed : -1, a ? a->agree : -1);

  r.runTo(8300);
  a = r.byId(idA);
  CHECK(a && a->st == TS_STILL && a->confirmed);
  printf("  t=8.3s : Ziel %d ruhend (LD2450 sieht noch), bestaetigt=%d\n", idA, a ? a->confirmed : -1);

  r.runTo(11000);   // LD2450 sieht A seit 8,5 s nicht mehr; 2410 (still) + 2420 melden Praesenz
  a = r.byId(idA);
  CHECK(a && a->st == TS_HELD);
  CHECK(r.trk.count() == 1);
  if (a) {
    CHECK(fabsf(a->x - 600) < 120 && fabsf(a->y - 2500) < 120);
#if ENABLE_LD2420
    CHECK(a->confirmed && !a->agreeBy[S2450] && a->agreeBy[S2410] && a->agreeBy[S2420]);
#else
    CHECK(!a->confirmed && a->agree == 1 && !a->agreeBy[S2450] && a->agreeBy[S2410] && !a->agreeBy[S2420]);   // nur LD2410C: gehalten, aber nicht bestaetigt
#endif
  }
  printf("  t=11.0s: Ziel %d GEHALTEN bei (%.0f,%.0f), bestaetigt=%d, 2410=%d\n", idA,
         a ? a->x : 0, a ? a->y : 0, a ? a->confirmed : -1, a ? a->agreeBy[S2410] : -1);

  r.runTo(16000);
  a = r.byId(idA);
  CHECK(a && a->st == TS_HELD);
  CHECK(r.trk.count() == 2);
  bool hasMovingB = false; for (auto& t : r.trk.tr) if (t.used && t.id != idA && t.st == TS_MOVING) hasMovingB = true;
  CHECK(hasMovingB);
  printf("  t=16.0s: %d Ziele (A gehalten + B bewegt)\n", r.trk.count());

  r.runTo(24500);   // A beginnt bei 22 s wieder zu laufen -> gleiche ID, LD2450 sieht wieder
  a = r.byId(idA);
  CHECK(a && a->st == TS_MOVING);
  printf("  t=24.5s: Ziel %d wieder bewegt (gleiche ID)=%d\n", idA, a && a->st == TS_MOVING);

  r.runTo(36000);   // alle weg (ab ~29 s aus dem Sichtfeld) -> Spuren laufen aus
  CHECK(r.trk.count() == 0);
  printf("  t=36.0s: %d Ziele (alle abgelaufen)\n", r.trk.count());

  // Ohne langsame Sensoren (nur LD2450): stehendes Ziel verschwindet nach LOST_MS (kein Halten)
  printf("[Fusion ohne 2410/2420]\n");
  Rig n; n.runTo(11000, true, false, false);
  CHECK(n.trk.count() == 0);
  printf("  t=11s nur LD2450: %d Ziele (kein Halten ohne Bestaetigung)\n", n.trk.count());
  n.runTo(5000 + 40000 + 3000, true, false, false);   // laeuft weiter ohne Absturz

  // Nur 2410 (kein 2420): gehalten, aber nicht bestaetigt (nur 1 Sensor)
  printf("[Fusion nur LD2450 + LD2410C]\n");
  Rig m; m.runTo(11000, true, true, false);
  const Track* h = m.byId(1);
  CHECK(h && h->st == TS_HELD && !h->confirmed && h->agree == 1);
  printf("  t=11s: gehalten=%d bestaetigt=%d Sensoren=%d\n", h && h->st == TS_HELD, h ? h->confirmed : -1, h ? h->agree : -1);
  // Sensor faellt aus: Halten laeuft nach HOLD_GRACE ab
  m.runTo(11000 + FUSION_HOLD_GRACE_MS + 1500, true, false, false);
  CHECK(m.trk.count() == 0 || !m.byId(1) || m.byId(1)->st != TS_HELD || false);
  CHECK(m.byId(1) == nullptr);
  printf("  nach Ausfall von 2410: gehaltenes Ziel entfernt = %d\n", m.byId(1) == nullptr);

  // Praesenz ohne Spur (nur 2410/2420, kein LD2450) -> Orphan-Bogen
  printf("[Praesenz ohne LD2450]\n");
  Rig o; o.runTo(11000, false, true, true);
  CHECK(o.trk.count() == 0);
  bool orph = false; for (int s = 1; s <= 2; s++) for (auto& x : o.trk.orphan[s]) orph |= x.used;
#if SHOW_RANGE_ONLY_RINGS
  CHECK(orph);
#else
  CHECK(!orph);   // Standard: Entfernung ohne Richtung wird NICHT angezeigt
#endif
  printf("  Orphan-Bogen vorhanden = %d (SHOW_RANGE_ONLY_RINGS=%d)\n", orph, SHOW_RANGE_ONLY_RINGS);

  // Komplett ohne Sensoren: keine Spur, kein Absturz
  Rig z; z.runTo(5000, false, false, false); CHECK(z.trk.count() == 0);
}

// ---------- Frontfilter: nur Personen VOR dem Geraet ----------
static void feed450At(Tracker& t, uint32_t now, const int16_t (*xyv)[3], int n) {
  uint8_t f[30]; encode2450(f, xyv, n);
  Ld2450Parser p; for (int i = 0; i < 30; i++) p.feed(f[i]);
  t.onFrame2450(p.last, now);
}
static void feed2410At(Tracker& t, uint32_t now, uint16_t moveCm, uint16_t stillCm) {
  uint8_t f[23]; encode2410(f, (moveCm ? 1 : 0) | (stillCm ? 2 : 0), moveCm, moveCm ? 60 : 0, stillCm, stillCm ? 45 : 0, moveCm ? moveCm : stillCm);
  Ld2410Parser p; for (int i = 0; i < 23; i++) p.feed(f[i]);
  t.onReport2410(p.last, now);
}
static const Track* firstTrack(const Tracker& t) { for (auto& x : t.tr) if (x.used) return &x; return nullptr; }
static bool singleKept(int x, int y) {
  Tracker t; int16_t a[3][3] = {{(int16_t)x, (int16_t)y, -20}, {0, 0, 0}, {0, 0, 0}};
  feed450At(t, 100, a, 1); feed450At(t, 200, a, 1);   // neues Ziel braucht SPAWN_CONFIRM_FRAMES Frames (Ausreisser-Pruefung)
  return t.count() == 1;
}

static void test_front_filter() {
  printf("[Frontfilter: nur Ziele vor dem Geraet] (MIN_FRONT_Y=%d mm, MIN_RANGE=%d mm, FOV=+-%.0f Grad)\n", MIN_FRONT_Y_MM, MIN_RANGE_MM, (double)FRONT_HALF_FOV_DEG);
  // 1) Ziel hinter dem Geraet (y<0) wird verworfen, nie ein Track
  { Tracker t; int16_t a[3][3] = {{100, -800, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 1); t.tick(k * 100); }
    CHECK(t.count() == 0 && t.created == 0 && t.droppedFront == 30);
    printf("  Ziel hinten (0.1m,-0.8m), 30 Frames: Tracks=%d verworfen=%u\n", t.count(), (unsigned)t.droppedFront); }
  // 2) Ziel vorn bleibt
  { Tracker t; int16_t a[3][3] = {{200, 2000, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 10; k++) { feed450At(t, k * 100, a, 1); t.tick(k * 100); }
    CHECK(t.count() == 1 && t.droppedFront == 0);
    printf("  Ziel vorn (0.2m,2.0m): Tracks=%d verworfen=%u\n", t.count(), (unsigned)t.droppedFront); }
  // 3) gemischter Frame: hinten + vorn -> genau ein Track, der vordere
  { Tracker t; int16_t a[3][3] = {{0, -600, -30}, {-300, 1800, -30}, {0, 0, 0}};
    feed450At(t, 100, a, 2); feed450At(t, 200, a, 2);
    const Track* k = firstTrack(t);
    CHECK(t.count() == 1 && k && fabsf(k->y - 1800) < 1 && t.droppedFront == 2); }
  // 4) Grenzen: y <= MIN_FRONT_Y, Blindzone, Winkel
  CHECK(!singleKept(0, 0 + MIN_FRONT_Y_MM));          // y == MIN_FRONT_Y_MM -> verworfen (y <= Wert)
  CHECK(!singleKept(0, -1));
  CHECK(!singleKept(0, MIN_RANGE_MM - 1));            // Blindzone
  CHECK(singleKept(0, MIN_RANGE_MM));                 // genau an der Grenze: bleibt
  CHECK(!singleKept(3000, 1000));                     // ~71 Grad: ausserhalb des Kegels
  CHECK(!singleKept(-1804, 1000));                    // ~61 Grad links
  CHECK(singleKept(1664, 1000));                      // ~59 Grad rechts
  CHECK(singleKept(-1000, 3000));
  CHECK(!singleKept(2500, 0));                        // seitlich auf Hoehe der Platine
  // 5) LD2410C allein erzeugt NIE einen Track (auch nicht lange, auch nicht mit Bewegung/still)
  { Tracker t;
    for (uint32_t k = 1; k <= 100; k++) { feed2410At(t, k * 100, (k % 2) ? 90 : 0, 120); t.tick(k * 100); }
    CHECK(t.count() == 0 && t.created == 0);
    bool orph = false; for (auto& x : t.orphan[S2410]) orph |= x.used;
#if SHOW_RANGE_ONLY_RINGS
    CHECK(orph);
#else
    CHECK(!orph);
#endif
    printf("  nur LD2410C (Nutzer hinter dem Geraet ~1 m): Tracks=%d Ringe=%d\n", t.count(), orph); }
  // 6) LD2450-Ziel hinten + LD2410C meldet dieselbe Person: weiterhin kein Track
  { Tracker t; int16_t a[3][3] = {{0, -900, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 1); feed2410At(t, k * 100, 90, 0); t.tick(k * 100); }
    CHECK(t.count() == 0);
    printf("  Ziel hinten + LD2410C bestaetigt: Tracks=%d\n", t.count()); }
  // 7) Vorn-Ziel bei 3 m + Nutzer hinten bei 1 m (LD2410C sieht nur 1 m): kein zweiter Track, Vorn-Ziel NICHT bestaetigt
  { Tracker t; int16_t a[3][3] = {{0, 3000, -30}, {0, -900, -30}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 2); feed2410At(t, k * 100, 100, 0); t.tick(k * 100); }
    const Track* f = firstTrack(t);
    CHECK(t.count() == 1 && f && !f->confirmed && f->agree == 1);
    printf("  vorn 3 m + Nutzer hinten 1 m: Tracks=%d bestaetigt=%d\n", t.count(), f ? f->confirmed : -1); }
  // 8) Vorn-Ziel bei 3 m, LD2410C meldet 3 m: bestaetigt (>=2 Sensoren)
  { Tracker t; int16_t a[3][3] = {{0, 3000, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 1); feed2410At(t, k * 100, 300, 0); t.tick(k * 100); }
    const Track* f = firstTrack(t);
    CHECK(t.count() == 1 && f && f->confirmed && f->agree == 2);
    printf("  vorn 3 m + LD2410C 3 m: bestaetigt=%d\n", f ? f->confirmed : -1); }
  // 10) Rueckkeule: Nutzer hinten wird gespiegelt mit y>0 im Nahbereich gemeldet (HLK: Y immer positiv) -> Nahzone verworfen
  { Tracker t; int16_t a[3][3] = {{100, 600, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 1); t.tick(k * 100); }
    CHECK(t.count() == 0);
    printf("  Geister-Nutzer (0.1 m, 0.6 m) y>0: Tracks=%d\n", t.count()); }
  // 11) Vorn-Ziel 1.5 m + LD2410C meldet Nutzer bei 0.8 m (Naehe): darf NICHT bestaetigen (frueher: |1500-800|<=900 -> bestaetigt)
  { Tracker t; int16_t a[3][3] = {{0, 1500, -30}, {0, 0, 0}, {0, 0, 0}};
    for (uint32_t k = 1; k <= 30; k++) { feed450At(t, k * 100, a, 1); feed2410At(t, k * 100, 80, 0); t.tick(k * 100); }
    const Track* f = firstTrack(t);
    CHECK(t.count() == 1 && f && !f->confirmed && f->agree == 1);
    printf("  vorn 1.5 m + LD2410C 0.8 m (Nutzer): bestaetigt=%d\n", f ? f->confirmed : -1); }
  // 12) Ruhende Spur in der Nahzone (0.9 m) + LD2410C 0.9 m: wird NICHT gehalten (frueher: bis 10 min gehalten)
  { Tracker t; int16_t a[3][3] = {{0, 900, 0}, {0, 0, 0}, {0, 0, 0}};
    uint32_t now = 0;
    for (int k = 1; k <= 30; k++) { now = k * 100; feed450At(t, now, a, 1); feed2410At(t, now, 0, 90); t.tick(now); }
    uint32_t last = now;
    for (now = last + 10; now <= last + TRACK_LOST_MS + 200; now += 10) { feed2410At(t, now, 0, 90); t.tick(now); }
    CHECK(t.count() == 0);
    printf("  ruhende Nahzonen-Spur + LD2410C: Tracks=%d (0 = nicht gehalten)\n", t.count()); }
  // 13) Ferne Ziele (>FAR_RANGE) werden TRACK_LOST_FAR_MS gehalten, nahe nur TRACK_LOST_MS
  { Tracker t; int16_t a[3][3] = {{0, 4500, -30}, {0, 2000, -30}, {0, 0, 0}};
    uint32_t now = 0;
    for (int k = 1; k <= 6; k++) { now = k * 100; feed450At(t, now, a, 2); t.tick(now); }
    CHECK(t.count() == 2);
    uint32_t last = now, goneNear = 0, goneFar = 0;
    for (now = last + 10; now <= last + 2000; now += 10) { t.tick(now); int c = t.count();
      if (!goneNear && c < 2) goneNear = now - last; if (!goneFar && c == 0) goneFar = now - last; }
    CHECK(goneNear >= (uint32_t)TRACK_LOST_MS && goneNear <= (uint32_t)TRACK_LOST_MS + 10 && goneFar >= (uint32_t)TRACK_LOST_FAR_MS && goneFar <= (uint32_t)TRACK_LOST_FAR_MS + 10);
    printf("  Verlust nah %u ms / fern %u ms\n", (unsigned)goneNear, (unsigned)goneFar); }
  // 9) Ohne LD2450-Daten (nur LD2410C): keine Personen, nur Sensorstatus
  { Rig r; r.runTo(20000, false, true, true); CHECK(r.trk.count() == 0); }
}

// ---------- Latenz / Praezision: sofort uebernehmen, nicht glaetten, Ausreisser per Plausibilitaet ----------
static void test_latency_precision() {
  printf("[Latenz/Praezision] POS_SMOOTH_ALPHA=%.2f TRACK_LOST_MS=%d OUTLIER_JUMP_MM=%d SPAWN_CONFIRM_FRAMES=%d\n",
         (double)POS_SMOOTH_ALPHA, TRACK_LOST_MS, OUTLIER_JUMP_MM, SPAWN_CONFIRM_FRAMES);
  // 1) Neues Ziel: erst nach SPAWN_CONFIRM_FRAMES Frames sichtbar (Rauschen/Einzelmessung nie)
  { Tracker t; int16_t a[3][3] = {{500, 2000, -30}, {0, 0, 0}, {0, 0, 0}};
    feed450At(t, 100, a, 1);
#if SPAWN_CONFIRM_FRAMES >= 2
    CHECK(t.count() == 0);
    feed450At(t, 200, a, 1); CHECK(t.count() == 1);
    // Einzelner Geisterpunkt in einem Frame, danach wieder weg -> nie ein Track
    Tracker g; int16_t gh[3][3] = {{-700, 3500, 0}, {0, 0, 0}, {0, 0, 0}};
    feed450At(g, 100, gh, 1); feed450At(g, 200, gh, 0); feed450At(g, 300, gh, 0);
    CHECK(g.count() == 0 && g.created == 0);
    // Zwei Messungen weit auseinander in aufeinanderfolgenden Frames bestaetigen sich nicht
    Tracker h; int16_t h1[3][3] = {{-700, 3500, 0}, {0, 0, 0}, {0, 0, 0}}, h2[3][3] = {{900, 1000, 0}, {0, 0, 0}, {0, 0, 0}};
    feed450At(h, 100, h1, 1); feed450At(h, 200, h2, 1); CHECK(h.count() == 0);
#else
    CHECK(t.count() == 1);
#endif
  }
  // 2) Position = genau die neueste Messung (alpha 1.0): sofort im selben Aufruf, ohne Glaettung
  { Tracker t; int16_t a[3][3] = {{500, 2000, -30}, {0, 0, 0}, {0, 0, 0}}, b[3][3] = {{540, 2130, -30}, {0, 0, 0}, {0, 0, 0}};
    feed450At(t, 100, a, 1); feed450At(t, 200, a, 1); feed450At(t, 300, b, 1);
    const Track* k = firstTrack(t);
    CHECK(k && t.count() == 1);
    if (POS_SMOOTH_ALPHA >= 1.0f) CHECK(k && k->x == 540.0f && k->y == 2130.0f);   // exakt die Messung, kein Nachziehen
    else CHECK(k && fabsf(k->x - (500 + POS_SMOOTH_ALPHA * 40)) < 0.01f && fabsf(k->y - (2000 + POS_SMOOTH_ALPHA * 130)) < 0.01f);
    printf("  Messung (540,2130) -> Track (%.0f,%.0f) im selben Aufruf\n", k ? k->x : 0, k ? k->y : 0); }
  // 3) Ausreisser: einzelner Sprung > OUTLIER_JUMP_MM wird NICHT uebernommen; Rueckkehr zur alten Position bleibt glatt
  { Tracker t; int16_t a[3][3] = {{0, 3000, -30}, {0, 0, 0}, {0, 0, 0}}, j[3][3] = {{0, 3000 - OUTLIER_JUMP_MM - 400, -30}, {0, 0, 0}, {0, 0, 0}};
    feed450At(t, 100, a, 1); feed450At(t, 200, a, 1);
    feed450At(t, 300, j, 1);                                              // Sprung (unplausibel)
    const Track* k = firstTrack(t);
    feed450At(t, 400, a, 1); k = firstTrack(t);
#if SPAWN_CONFIRM_FRAMES >= 2
    CHECK(k && k->y == 3000.0f && t.count() == 1);
#endif
    printf("  Einzelsprung %d mm: Track bleibt bei y=%.0f\n", OUTLIER_JUMP_MM + 400, k ? k->y : 0);
#if SPAWN_CONFIRM_FRAMES >= 2
    // Sprung, der 2 Frames in Folge bestaetigt wird, wird als neues Ziel uebernommen
    Tracker u; feed450At(u, 100, a, 1); feed450At(u, 200, a, 1);
    feed450At(u, 300, j, 1); feed450At(u, 400, j, 1);
    bool atJump = false; for (auto& x : u.tr) if (x.used && x.y == (float)j[0][1]) atJump = true;
    CHECK(atJump);
    printf("  Sprung 2 Frames in Folge bestaetigt: uebernommen=%d\n", atJump);
#endif
    // Plausible schnelle Bewegung (<= OUTLIER_JUMP_MM) wird sofort gefolgt
    Tracker w; int16_t s[3][3] = {{0, 3000, -30}, {0, 0, 0}, {0, 0, 0}}, m2[3][3] = {{0, 3000 - 800, -30}, {0, 0, 0}, {0, 0, 0}};
    feed450At(w, 100, s, 1); feed450At(w, 200, s, 1); feed450At(w, 300, m2, 1);
    const Track* q = firstTrack(w); CHECK(q && w.count() == 1 && (POS_SMOOTH_ALPHA < 1.0f || q->y == 2200.0f)); }
  // 4) Lost-Target: nicht mehr gemeldetes Ziel verschwindet nach TRACK_LOST_MS (kein "Halten" fuer bewegte Ziele)
  { Tracker t; int16_t a[3][3] = {{0, 3000, -30}, {0, 0, 0}, {0, 0, 0}};
    uint32_t now = 0;
    for (int k = 1; k <= 6; k++) { now = k * 100; feed450At(t, now, a, 1); feed2410At(t, now, 300, 0); t.tick(now); }
    CHECK(t.count() == 1);
    uint32_t last = now, gone = 0;
    for (now = last + 10; now <= last + 2000; now += 10) { t.tick(now); if (!gone && t.count() == 0) gone = now - last; }
    CHECK(gone > 0 && gone <= (uint32_t)TRACK_LOST_MS + 10 && gone >= (uint32_t)TRACK_LOST_MS);
    printf("  bewegtes Ziel verloren: ausgeblendet nach %u ms (TRACK_LOST_MS=%d)\n", (unsigned)gone, TRACK_LOST_MS); }
  // 5) Ruhendes Ziel + LD2410C bestaetigt: wird gehalten (nicht bestaetigt); ohne LD2410C sofort weg
  { Tracker t; int16_t a[3][3] = {{0, 3000, 0}, {0, 0, 0}, {0, 0, 0}};
    uint32_t now = 0;
    for (int k = 1; k <= 30; k++) { now = k * 100; feed450At(t, now, a, 1); feed2410At(t, now, 0, 300); t.tick(now); }
    const Track* h = firstTrack(t); CHECK(h && h->st == TS_STILL);
    uint32_t last = now;
    for (now = last + 10; now <= last + TRACK_LOST_MS + 200; now += 10) { feed2410At(t, now, 0, 300); t.tick(now); }
    h = firstTrack(t); CHECK(h && h->st == TS_HELD);
    Tracker u; now = 0;
    for (int k = 1; k <= 30; k++) { now = k * 100; feed450At(u, now, a, 1); u.tick(now); }
    last = now; for (now = last + 10; now <= last + TRACK_LOST_MS + 50; now += 10) u.tick(now);
    CHECK(u.count() == 0);
    printf("  ruhend + LD2410C: gehalten=%d; ruhend ohne LD2410C: weg=%d\n", h && h->st == TS_HELD, u.count() == 0); }
  // 6) Verarbeitungsrate / Latenz am PC (Parser + Fusion, Bytes -> Track)
  { Ld2450Parser p; Tracker t; uint8_t f[30]; int16_t a[3][3] = {{500, 2000, -30}, {-800, 3500, -20}, {0, 0, 0}};
    const int N = 200000; uint32_t now = 0; double maxUs = 0; int upd = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i++) {
      a[0][0] = 500 + (i % 50); a[0][1] = 2000 + (i % 70); encode2450(f, a, 2); now += 100;
      auto s0 = std::chrono::steady_clock::now();
      for (int b = 0; b < 30; b++) if (p.feed(f[b])) { t.onFrame2450(p.last, now); upd++; }
      t.tick(now);
      double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - s0).count(); if (us > maxUs) maxUs = us;
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(upd == N && t.count() == 2);
    CHECK(N / sec > 20000);   // mind. 2000x schneller als die 10 Hz der Sensoren (auf dem PC)
    printf("  Verarbeitung am PC: %.0f Frames/s (%.2f us/Frame, max %.1f us); jeder Frame -> Track im selben Aufruf (0 ms Verzoegerung)\n", N / sec, sec / N * 1e6, maxUs);
    printf("  ESP32 (240 MHz) ~10-30x langsamer geschaetzt: weit unter 1 ms je Frame (nicht gemessen)\n"); }
}

static void test_status() {
  printf("[Sensorstatus]\n");
  SensorLink l;
  CHECK(l.state(1000, 1500) == LINK_NONE);
  l.note(1000);
  CHECK(l.state(2000, 1500) == LINK_OK);
  CHECK(l.state(3000, 1500) == LINK_NONE);
  CHECK(l.state(3000, 1500, true) == LINK_IDLE);
}

int main() {
  test_ld2450(); test_ld2410(); test_ld2420(); test_fusion(); test_front_filter(); test_latency_precision(); test_status();
  printf("\n%d Pruefungen, %d fehlgeschlagen\n", checks, fails);
  return fails ? 1 : 0;
}
