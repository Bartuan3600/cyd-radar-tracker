// UI-Zeichencode (Radar, Sensor-Details, Einstellungen, Buttons) als Template ueber die Grafik-Schnittstelle.
// Auf dem Geraet ist G = TFT_eSprite (320x240, 8 Bit), am PC eine Mock-Klasse (test/host_ui.cpp),
// die die Zeichenbefehle aufzeichnet -> tools/render_ui.py macht daraus PNGs.
// Nur ASCII-Text (die TFT_eSPI-Standardfonts haben keine Umlaute).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "fusion.h"
#include "ld_parsers.h"
#include "../include/config.h"

// --- RGB565-Farben ---
#define UC_BG      0x0000
#define UC_WHITE   0xFFFF
#define UC_GRID    0x0320   // dunkelgruen
#define UC_GRID2   0x07E0   // helles Gruen (Rand)
#define UC_ORANGE  0xFD20   // unbestaetigt
#define UC_GREEN   0x07E0   // bestaetigt
#define UC_RED     0xF800
#define UC_YELLOW  0xFFE0
#define UC_CYAN    0x07FF
#define UC_MAGENTA 0xF81F
#define UC_GREY    0x7BEF
#define UC_BTN     0x0010   // dunkelblau
#define UC_BTN_ON  0x0400   // dunkelgruen

enum UiPage : uint8_t { PG_RADAR = 0, PG_DETAIL = 1, PG_SET = 2, PG_COUNT = 3 };
enum BtnId : uint8_t { B_NONE = 0, B_ZOOM, B_PAGE, B_DEMO, B_RESET, B_TOL_M, B_TOL_P, B_HOLD_M, B_HOLD_P, B_MIRROR, B_DEFAULTS };

struct UiState {
  uint8_t page = PG_RADAR;
  bool zoom6 = true;
  bool demo = false;
  bool mirrorX = false;
};

struct Btn { uint8_t id; int16_t x, y, w, h; const char* label; char sub[16]; uint16_t color; };

struct UiView {
  const Tracker* trk;
  const Ld2450Parser* p450; const Ld2410Parser* p410;
#if ENABLE_LD2420
  const Ld2420Parser* p420;
#endif
  const SensorLink* link[3];
  LinkState ls[3];
  uint32_t now;
  const UiState* ui;
  // Messwerte am Geraet (0 = nicht anzeigen): Radar-FPS*10, Zeichenzeit ms, LD2450-Frames/s*10, Latenz Frame->Display ms
  uint16_t fps10 = 0, renderMs = 0, frameHz10 = 0, latMs = 0;
};

#define RADAR_OX 120
#define RADAR_OY 234
#define RADAR_R  136

static inline void btnSet(Btn& b, uint8_t id, int x, int y, int w, int h, const char* label, const char* sub, uint16_t col) {
  b.id = id; b.x = x; b.y = y; b.w = w; b.h = h; b.label = label; b.color = col;
  snprintf(b.sub, sizeof(b.sub), "%s", sub ? sub : "");
}

// Buttons je Seite (auch fuer Touch-Hit-Test). Rueckgabe: Anzahl.
static inline int uiButtons(const UiState& u, const Tracker& /*trk*/, Btn* b) {
  int n = 0;
  char s[16];
  const int X = 244, W = 74, H = 66;
  if (u.page == PG_RADAR) {
    btnSet(b[n++], B_ZOOM, X, 24, W, H, "ZOOM", u.zoom6 ? "6 m" : "3 m", UC_BTN);
    btnSet(b[n++], B_PAGE, X, 96, W, H, "SEITE", "Radar", UC_BTN);
    btnSet(b[n++], B_DEMO, X, 168, W, H, "DEMO", u.demo ? "AN" : "aus", u.demo ? UC_BTN_ON : UC_BTN);
  } else if (u.page == PG_DETAIL) {
    btnSet(b[n++], B_RESET, X, 24, W, H, "RESET", "Ziele", UC_BTN);
    btnSet(b[n++], B_PAGE, X, 96, W, H, "SEITE", "Sensor", UC_BTN);
    btnSet(b[n++], B_DEMO, X, 168, W, H, "DEMO", u.demo ? "AN" : "aus", u.demo ? UC_BTN_ON : UC_BTN);
  } else {
    btnSet(b[n++], B_DEFAULTS, X, 24, W, H, "STD", "Werte", UC_BTN);
    btnSet(b[n++], B_PAGE, X, 96, W, H, "SEITE", "Einst.", UC_BTN);
    btnSet(b[n++], B_DEMO, X, 168, W, H, "DEMO", u.demo ? "AN" : "aus", u.demo ? UC_BTN_ON : UC_BTN);
    btnSet(b[n++], B_TOL_M, 124, 24, 54, 58, "-", "", UC_BTN);
    btnSet(b[n++], B_TOL_P, 184, 24, 54, 58, "+", "", UC_BTN);
    btnSet(b[n++], B_HOLD_M, 124, 92, 54, 58, "-", "", UC_BTN);
    btnSet(b[n++], B_HOLD_P, 184, 92, 54, 58, "+", "", UC_BTN);
    snprintf(s, sizeof(s), "%s", u.mirrorX ? "AN" : "aus");
    btnSet(b[n++], B_MIRROR, 124, 160, 114, 58, "X SPIEGELN", s, u.mirrorX ? UC_BTN_ON : UC_BTN);
  }
  return n;
}

template <class G> void uiDrawButton(G& g, const Btn& b) {
  g.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.color);
  g.drawRoundRect(b.x, b.y, b.w, b.h, 8, UC_WHITE);
  g.setTextColor(UC_WHITE);
  g.setTextDatum(4);  // MC_DATUM
  bool wide = b.w > 100;
  if (b.sub[0]) {
    g.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2 - 11, wide ? 2 : 4);
    g.drawString(b.sub, b.x + b.w / 2, b.y + b.h / 2 + 15, 2);
  } else {
    g.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 4);
  }
}

template <class G> void uiDrawStatusBar(G& g, const UiView& v) {
  g.fillRect(0, 0, 320, 21, UC_BG);
  static const char* names[3] = {"LD2450", "LD2410C", "LD2420"};
  g.setTextDatum(0);  // TL
  for (int i = 0; i < FUSION_NSENS; i++) {
    int x0 = 3 + i * (FUSION_NSENS == 2 ? 100 : 88);
    uint16_t c = v.ls[i] == LINK_OK ? UC_GREEN : (v.ls[i] == LINK_IDLE ? UC_YELLOW : UC_RED);
    g.fillCircle(x0 + 6, 10, 5, c);
    g.setTextColor(UC_WHITE);
    g.drawString(names[i], x0 + 15, 3, 2);
  }
  if (v.ui->demo) { g.setTextColor(UC_CYAN); g.drawString("SIM", 284, 3, 2); }
  g.drawFastHLine(0, 21, 320, UC_GREY);
}

static inline void uiPolar(const UiView& v, float xmm, float ymm, int& px, int& py) {
  float zoomMm = (v.ui->zoom6 ? RADAR_ZOOM_B_M : RADAR_ZOOM_A_M) * 1000.0f;
  float sc = RADAR_R / zoomMm;
  float sx = v.ui->mirrorX ? -1.0f : 1.0f;
  px = RADAR_OX + (int)lroundf(sx * xmm * sc);
  py = RADAR_OY - (int)lroundf(ymm * sc);
}

template <class G> void uiArc(G& g, float rpx, uint16_t col, int stepDeg, bool dotted) {
  const float d2r = 3.14159265f / 180.0f;
  int a = -(int)RADAR_HALF_FOV_DEG;
  int px = 0, py = 0; bool have = false; int k = 0;
  for (; a <= (int)RADAR_HALF_FOV_DEG; a += stepDeg, k++) {
    int x = RADAR_OX + (int)lroundf(rpx * sinf(a * d2r));
    int y = RADAR_OY - (int)lroundf(rpx * cosf(a * d2r));
    if (have && (!dotted || (k & 1))) g.drawLine(px, py, x, y, col);
    px = x; py = y; have = true;
  }
}

// Statischer Radar-Hintergrund (Gitter, Ringe, Legende): haengt nur von Zoom/Seite ab, wird auf dem Geraet einmal in einen Sprite gezeichnet.
template <class G> void uiDrawRadarBg(G& g, const UiView& v) {
  const UiState& u = *v.ui;
  const float d2r = 3.14159265f / 180.0f;
  g.fillRect(0, 22, 244, 218, UC_BG);
  int zoomM = u.zoom6 ? RADAR_ZOOM_B_M : RADAR_ZOOM_A_M;
  float sc = RADAR_R / (zoomM * 1000.0f);
  int ringStep = u.zoom6 ? 2 : 1;
  g.setTextDatum(0); g.setTextColor(UC_GREY);
  for (int m = ringStep; m < zoomM; m += ringStep) {
    float rp = m * 1000.0f * sc;
    uiArc(g, rp, UC_GRID, 3, false);
    char s[8]; snprintf(s, sizeof(s), "%dm", m);
    g.drawString(s, RADAR_OX + 3, RADAR_OY - (int)rp - 9, 1);
  }
  for (int a = -60; a <= 60; a += 30) {
    int x = RADAR_OX + (int)lroundf(RADAR_R * sinf(a * d2r));
    int y = RADAR_OY - (int)lroundf(RADAR_R * cosf(a * d2r));
    g.drawLine(RADAR_OX, RADAR_OY, x, y, (a == 0 || a == 60 || a == -60) ? (a == 0 ? UC_GRID : UC_GRID2) : UC_GRID);
  }
  uiArc(g, (float)RADAR_R, UC_GRID2, 3, false);
  g.fillCircle(RADAR_OX, RADAR_OY, 3, UC_WHITE);
  // Legende
  int ly = 54;
  g.setTextColor(UC_GREY);
  g.fillCircle(8, ly + 4, 4, UC_ORANGE);               g.drawString("bewegt", 18, ly, 1); ly += 11;
  g.drawCircle(8, ly + 4, 4, UC_ORANGE);               g.drawString("ruhend (LD2450 sieht)", 18, ly, 1); ly += 11;
  g.drawCircle(8, ly + 4, 4, UC_ORANGE); g.fillCircle(8, ly + 4, 1, UC_ORANGE); g.drawString(ENABLE_LD2420 ? "gehalten (2410/2420)" : "gehalten (LD2410C)", 18, ly, 1); ly += 11;
  g.fillCircle(8, ly + 4, 4, UC_GREEN);                g.drawString("gruen = bestaetigt (>=2 Sensoren)", 18, ly, 1); ly += 11;
  g.setTextColor(UC_GREY); g.drawString("nur Ziele vor dem Geraet", 18, ly, 1);
}

// Dynamischer Teil: nur Ziele (jede Messung als eigener Punkt, keine Spur, keine Glaettung) + Info. Wird je Frame ueber den Hintergrund gezeichnet.
template <class G> void uiDrawRadarFg(G& g, const UiView& v) {
  const UiState& u = *v.ui;
  int zoomM = u.zoom6 ? RADAR_ZOOM_B_M : RADAR_ZOOM_A_M;
  float sc = RADAR_R / (zoomM * 1000.0f);
  (void)sc;
#if SHOW_RANGE_ONLY_RINGS
  // Praesenz ohne zugeordnete Spur (nur 2410/2420, Richtung unbekannt): gestrichelter Bogen. Standard AUS (zeigt sonst auch den Nutzer hinter dem Geraet).
  for (int s = 1; s < FUSION_NSENS; s++) for (int k = 0; k < 2; k++) {
    const Orphan& o = v.trk->orphan[s][k];
    if (!o.used || o.distMm > zoomM * 1000) continue;
    uiArc(g, o.distMm * sc, s == 1 ? UC_MAGENTA : UC_CYAN, 4, true);
  }
#endif

#if SHOW_RAW_DOTS
  // Rohe LD2450-Messpunkte im Frontbereich (wie weisse Ringe im PC-Viewer): zeigt auch Ziele, die noch keine Spur sind.
  for (int i = 0; i < 3; i++) {
    const Target2450& rt = v.p450->last.t[i];
    if (!rt.valid || !Tracker::frontOk((float)rt.x, (float)rt.y)) continue;
    if (sqrtf((float)rt.x * rt.x + (float)rt.y * rt.y) > zoomM * 1000.0f) continue;
    int rx, ry; uiPolar(v, (float)rt.x, (float)rt.y, rx, ry);
    g.drawCircle(rx, ry, 3, UC_WHITE);
  }
#endif
  int outside = 0;
  for (int i = 0; i < FUSION_MAX_TRACKS; i++) {
    const Track& t = v.trk->tr[i];
    if (!t.used) continue;
    if (t.range() > zoomM * 1000.0f) { outside++; continue; }
    int px, py; uiPolar(v, t.x, t.y, px, py);
    uint16_t col = t.confirmed ? UC_GREEN : UC_ORANGE;
    if (t.st == TS_MOVING) {
      g.fillCircle(px, py, 6, col);
    } else {
      g.drawCircle(px, py, 7, col); g.drawCircle(px, py, 6, col);
      if (t.st == TS_HELD) g.fillCircle(px, py, 2, col);
    }
    char s[8]; snprintf(s, sizeof(s), "%u", t.id);
    g.setTextColor(UC_WHITE); g.setTextDatum(0);
    g.drawString(s, px + 9, py - 4, 1);
  }

  // Info oben links
  char s[40];
  g.setTextDatum(0); g.setTextColor(UC_WHITE);
  snprintf(s, sizeof(s), "Ziele %d  best. %d", v.trk->count(), v.trk->countConfirmed());
  g.drawString(s, 4, 25, 2);
  if (outside) { snprintf(s, sizeof(s), "+%d ausser Reichw.", outside); g.setTextColor(UC_YELLOW); g.drawString(s, 4, 41, 1); }
  if (v.fps10) {   // Messwerte am Geraet (links unten, ausserhalb des Sichtkegels)
    g.setTextColor(UC_GREY); g.setTextDatum(0);
    snprintf(s, sizeof(s), "Anz %u.%u fps %ums", v.fps10 / 10, v.fps10 % 10, v.renderMs);
    g.drawString(s, 4, 214, 1);
    snprintf(s, sizeof(s), "LD2450 %u.%u/s lat %ums", v.frameHz10 / 10, v.frameHz10 % 10, v.latMs);
    g.drawString(s, 4, 225, 1);
  }
}

template <class G> void uiDrawRadar(G& g, const UiView& v) { uiDrawRadarBg(g, v); uiDrawRadarFg(g, v); }

template <class G> void uiDrawDetail(G& g, const UiView& v) {
  g.fillRect(0, 22, 244, 218, UC_BG);
  char s[48];
  g.setTextDatum(0);
  int y = 24;
  auto head = [&](const char* name, int i, uint32_t fr, uint32_t rs) {
    uint16_t c = v.ls[i] == LINK_OK ? UC_GREEN : (v.ls[i] == LINK_IDLE ? UC_YELLOW : UC_RED);
    g.fillCircle(6, y + 8, 4, c);
    g.setTextColor(UC_CYAN);
    snprintf(s, sizeof(s), "%s Fr %lu rs %lu", name, (unsigned long)fr, (unsigned long)rs);
    g.drawString(s, 14, y, 2); y += 17;
    g.setTextColor(UC_WHITE);
  };
  head("LD2450", S2450, v.p450->frames, v.p450->resyncs);
  for (int i = 0; i < 3; i++) {
    const Target2450& t = v.p450->last.t[i];
    if (t.valid) snprintf(s, sizeof(s), "T%d x%d y%d v%d", i + 1, t.x, t.y, t.speed);
    else snprintf(s, sizeof(s), "T%d -", i + 1);
    g.drawString(s, 8, y, 2); y += 16;
  }
  y += 4;
  const Report2410& r = v.p410->last;
  head("LD2410C", S2410, v.p410->frames, v.p410->resyncs);
  snprintf(s, sizeof(s), "st %u bew %ucm/%u", r.state, r.moveDist, r.moveEnergy); g.drawString(s, 8, y, 2); y += 16;
  snprintf(s, sizeof(s), "still %ucm/%u det %ucm", r.stillDist, r.stillEnergy, r.detDist); g.drawString(s, 8, y, 2); y += 16;
  y += 4;
#if ENABLE_LD2420
  const Report2420& q = v.p420->last;
  head("LD2420", S2420, v.p420->lines + v.p420->frames, v.p420->resyncs);
  snprintf(s, sizeof(s), "%s  Range %ucm", q.presence ? "ON" : "OFF", q.rangeValid ? q.range : 0); g.drawString(s, 8, y, 2); y += 16;
  snprintf(s, sizeof(s), "Bytes %lu", (unsigned long)v.p420->bytes); g.drawString(s, 8, y, 2); y += 16;
#endif
  g.setTextColor(UC_GREY);
  snprintf(s, sizeof(s), "Frontfilter: %lu verworfen", (unsigned long)v.trk->droppedFront); g.drawString(s, 8, y, 2); y += 16;
  snprintf(s, sizeof(s), "Ausreisser: %lu verworfen", (unsigned long)v.trk->outliers); g.drawString(s, 8, y, 2);
}

template <class G> void uiDrawSettings(G& g, const UiView& v) {
  g.fillRect(0, 22, 244, 218, UC_BG);
  char s[32];
  g.setTextDatum(0); g.setTextColor(UC_WHITE);
  g.drawString("Toleranz", 6, 28, 2);
  snprintf(s, sizeof(s), "%d mm", v.trk->cfg.tol2410); g.setTextColor(UC_CYAN); g.drawString(s, 6, 50, 4);
  g.setTextColor(UC_WHITE); g.drawString("Halten", 6, 96, 2);
  snprintf(s, sizeof(s), "%lu.%lu s", (unsigned long)(v.trk->cfg.holdGraceMs / 1000), (unsigned long)((v.trk->cfg.holdGraceMs % 1000) / 100));
  g.setTextColor(UC_CYAN); g.drawString(s, 6, 118, 4);
  g.setTextColor(UC_GREY); g.drawString("Sensor-X", 6, 164, 2); g.drawString("spiegeln", 6, 180, 2);
}

template <class G> void uiDrawAll(G& g, const UiView& v) {
  uiDrawStatusBar(g, v);
  if (v.ui->page == PG_RADAR) uiDrawRadar(g, v);
  else if (v.ui->page == PG_DETAIL) uiDrawDetail(g, v);
  else uiDrawSettings(g, v);
  g.fillRect(240, 22, 80, 218, UC_BG);
  Btn b[10]; int n = uiButtons(*v.ui, *v.trk, b);
  for (int i = 0; i < n; i++) uiDrawButton(g, b[i]);
}

// ---- Teilzeichnen auf dem Geraet ----
// Der Seiteninhalt (x 0..239, y 22..239) liegt in einem eigenen Sprite (y um 22 verschoben); Statusleiste und rechte Buttons
// werden separat und nur bei Aenderung gezeichnet. So wird je Frame nur der Radarbereich (240x218) uebertragen statt 320x240.
#define UI_REGION_W 240
#define UI_REGION_H 218
#define UI_REGION_Y 22

template <class G> struct UiShift {   // verschiebt alle y-Koordinaten um dy
  G& g; int dy;
  void fillRect(int x, int y, int w, int h, uint16_t c) { g.fillRect(x, y + dy, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { g.fillRoundRect(x, y + dy, w, h, r, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { g.drawRoundRect(x, y + dy, w, h, r, c); }
  void fillCircle(int x, int y, int r, uint16_t c) { g.fillCircle(x, y + dy, r, c); }
  void drawCircle(int x, int y, int r, uint16_t c) { g.drawCircle(x, y + dy, r, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { g.drawLine(x0, y0 + dy, x1, y1 + dy, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { g.drawFastHLine(x, y + dy, w, c); }
  void setTextColor(uint16_t c) { g.setTextColor(c); }
  void setTextDatum(int d) { g.setTextDatum(d); }
  void drawString(const char* s, int x, int y, int font) { g.drawString(s, x, y + dy, font); }
};

// Statischer Radar-Hintergrund in den Region-Sprite (rg = Sprite, Koordinaten wie im Vollbild).
template <class G> void uiDrawRegionBg(G& rg, const UiView& v) { UiShift<G> s{rg, -UI_REGION_Y}; uiDrawRadarBg(s, v); }
// Dynamischer Teil (Radar) bzw. kompletter Seiteninhalt (Detail/Einstellungen) in den Region-Sprite.
template <class G> void uiDrawRegionFg(G& rg, const UiView& v) {
  UiShift<G> s{rg, -UI_REGION_Y};
  if (v.ui->page == PG_RADAR) { uiDrawRadarFg(s, v); return; }
  if (v.ui->page == PG_DETAIL) uiDrawDetail(s, v); else uiDrawSettings(s, v);
  Btn b[10]; int n = uiButtons(*v.ui, *v.trk, b);
  for (int i = 0; i < n; i++) if (b[i].x + b[i].w <= UI_REGION_W) uiDrawButton(s, b[i]);   // Einstellungs-Buttons liegen im Seiteninhalt
}
// Statusleiste (320x22) in einen Sprite (oder direkt aufs Display).
template <class G> void uiDrawStatusOnly(G& g, const UiView& v) { uiDrawStatusBar(g, v); }
// Rechte Buttonspalte direkt aufs Display (nur bei Seiten-/Zustandswechsel).
template <class G> void uiDrawRightColumn(G& g, const UiView& v) {
  g.fillRect(UI_REGION_W, UI_REGION_Y, 320 - UI_REGION_W, UI_REGION_H, UC_BG);
  Btn b[10]; int n = uiButtons(*v.ui, *v.trk, b);
  for (int i = 0; i < n; i++) if (b[i].x + b[i].w > UI_REGION_W) uiDrawButton(g, b[i]);
}
