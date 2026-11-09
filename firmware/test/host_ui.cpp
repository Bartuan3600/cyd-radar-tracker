// Host-Mock der Grafik: fuehrt die echte UI-Zeichenlogik (src/ui.h) mit dem Fusionsszenario aus
// und schreibt die Zeichenbefehle als Textdatei -> tools/render_ui.py macht PNGs.
#include <stdio.h>
#include <string>
#include "../src/ld_parsers.h"
#include "../src/fusion.h"
#include "../src/sim.h"
#include "../src/ui.h"

struct Mock {
  FILE* f;
  void fillRect(int x, int y, int w, int h, uint16_t c) { fprintf(f, "fillRect %d %d %d %d %u\n", x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { fprintf(f, "fillRoundRect %d %d %d %d %d %u\n", x, y, w, h, r, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { fprintf(f, "drawRoundRect %d %d %d %d %d %u\n", x, y, w, h, r, c); }
  void fillCircle(int x, int y, int r, uint16_t c) { fprintf(f, "fillCircle %d %d %d %u\n", x, y, r, c); }
  void drawCircle(int x, int y, int r, uint16_t c) { fprintf(f, "drawCircle %d %d %d %u\n", x, y, r, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) { fprintf(f, "drawLine %d %d %d %d %u\n", x0, y0, x1, y1, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fprintf(f, "drawLine %d %d %d %d %u\n", x, y, x + w - 1, y, c); }
  void setTextColor(uint16_t c) { fprintf(f, "color %u\n", c); }
  void setTextDatum(int d) { fprintf(f, "datum %d\n", d); }
  void drawString(const char* s, int x, int y, int font) { fprintf(f, "text %d %d %d %s\n", x, y, font, s); }
};

struct Rig {
  Ld2450Parser a; Ld2410Parser b; Tracker trk; uint32_t now = 0;
  SensorLink l450, l410;
#if ENABLE_LD2420
  Ld2420Parser c; SensorLink l420;
#endif
  void runTo(uint32_t t) {
    while (now < t) {
      now += 20;
      if (now % 100 == 0) {
        SimBytes sb; simStep(now, sb);
        for (int i = 0; i < sb.n2450; i++) if (a.feed(sb.b2450[i])) { trk.onFrame2450(a.last, now); l450.note(now); }
        for (int i = 0; i < sb.n2410; i++) if (b.feed(sb.b2410[i])) { trk.onReport2410(b.last, now); l410.note(now); }
#if ENABLE_LD2420
        for (int i = 0; i < sb.n2420; i++) if (c.feed((uint8_t)sb.b2420[i])) { trk.onReport2420(c.last, now); l420.note(now); }
#endif
      }
      trk.tick(now);
    }
  }
};

static void shot(Rig& r, UiState ui, const char* name) {
  std::string fn = std::string("/tmp/ui_") + name + ".txt";
  Mock m; m.f = fopen(fn.c_str(), "w");
  UiView v; v.trk = &r.trk; v.p450 = &r.a; v.p410 = &r.b;
  v.link[0] = &r.l450; v.link[1] = &r.l410; v.link[2] = &r.l410;
  v.ls[0] = r.l450.state(r.now, LINK_TIMEOUT_MS);
  v.ls[1] = r.l410.state(r.now, LINK_TIMEOUT_MS);
  v.ls[2] = LINK_NONE;
#if ENABLE_LD2420
  v.p420 = &r.c; v.link[2] = &r.l420;
  v.ls[2] = r.l420.state(r.now, LINK_TIMEOUT_MS, !r.c.last.presence);
#endif
  v.now = r.now; v.ui = &ui;
  v.fps10 = 252; v.renderMs = 28; v.frameHz10 = 100; v.latMs = 31;   // Beispielwerte fuer die Geraete-Messzeile
  m.fillRect(0, 0, 320, 240, UC_BG);
  uiDrawAll(m, v);
  { Mock n; n.f = fopen("/dev/null", "w"); uiDrawRegionBg(n, v); uiDrawRegionFg(n, v); uiDrawStatusOnly(n, v); uiDrawRightColumn(n, v); fclose(n.f); }   // Teilzeichen-Pfad (Geraet) kompiliert/laeuft
  fclose(m.f);
  printf("%s\n", fn.c_str());
}

int main() {
  Rig r; UiState ui; ui.demo = true;
  r.runTo(3000);  ui.zoom6 = true;  shot(r, ui, "radar_moving_6m");
  r.runTo(11000); shot(r, ui, "radar_held_6m");
  ui.zoom6 = false; shot(r, ui, "radar_held_3m");
  ui.zoom6 = true; r.runTo(16000); shot(r, ui, "radar_two");
  ui.page = PG_DETAIL; shot(r, ui, "detail");
  ui.page = PG_SET; shot(r, ui, "settings");
  Rig e; UiState u2; e.runTo(1000);  // ohne Sensoren: alles rot
  e.now = 5000; e.trk.tick(5000); shot(e, u2, "radar_nosensor");
  return 0;
}
