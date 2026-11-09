// Host-Test der JSON-Ausgabe fuer den PC-Viewer (state_json.h, 2 Sensoren, Frontfilter) im Simulationsmodus: gleiche Parser + Fusion wie auf dem Geraet.
// Schreibt Schnappschuesse nach <outdir>/state_<ms>.json; tools/validate_json.py prueft sie mit json.loads.
#include <stdio.h>
#include <string.h>
#include <string>
#if ENABLE_LD2420
#error "JSON-Test nur fuer die Standardkonfiguration (2 Sensoren)"
#endif
#include "../src/state_json.h"
#include "../src/sim.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Rig {
  Ld2450Parser a; Ld2410Parser b; Tracker trk; SensorLink l450, l410; uint32_t now = 0;
  void runTo(uint32_t t) {
    while (now < t) {
      now += 20;
      if (now % 100 == 0) {
        SimBytes sb; simStep(now, sb);
        for (int i = 0; i < sb.n2450; i++) if (a.feed(sb.b2450[i])) { trk.onFrame2450(a.last, now); l450.note(now); }
        for (int i = 0; i < sb.n2410; i++) if (b.feed(sb.b2410[i])) { trk.onReport2410(b.last, now); l410.note(now); }
      }
      trk.tick(now);
    }
  }
  int json(char* buf, int cap, bool sim = true) {
    StateView v = {&trk, &a, &b, &l450, &l410, now, sim, 1500, 252, 28, 100, 31};
    return buildStateJson(buf, cap, v);
  }
};

static bool balanced(const char* s, int n) {
  int br = 0, sq = 0; bool str = false;
  for (int i = 0; i < n; i++) {
    char ch = s[i];
    if (ch == '"') str = !str;
    if (str) continue;
    if (ch == '{') br++; else if (ch == '}') br--; else if (ch == '[') sq++; else if (ch == ']') sq--;
    if (br < 0 || sq < 0) return false;
  }
  return br == 0 && sq == 0 && !str;
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "/tmp/cyd_json";
  char cmd[256]; snprintf(cmd, sizeof cmd, "mkdir -p %s", out); if (system(cmd)) return 2;
  static char buf[3072];
  Rig r; int maxlen = 0;
  // Schnappschuesse alle 500 ms ueber das 40-s-Szenario
  for (uint32_t t = 500; t <= 40000; t += 500) {
    r.runTo(t);
    int n = r.json(buf, sizeof buf);
    CHECK(n > 0 && n < (int)sizeof buf);
    CHECK(balanced(buf, n));
    if (n > maxlen) maxlen = n;
    char fn[300]; snprintf(fn, sizeof fn, "%s/state_%05u.json", out, (unsigned)t);
    FILE* f = fopen(fn, "w"); fwrite(buf, 1, n, f); fclose(f);
    if (t == 3000 || t == 11000 || t == 16000 || t == 36000) printf("  t=%.1fs  %d Byte: %.150s...\n", t / 1000.0, n, buf);
  }
  printf("  laengster Schnappschuss: %d Byte (Puffer 3072)\n", maxlen);
  // gezielte Pruefungen am Text
  { Rig q; q.runTo(11000); int n = q.json(buf, sizeof buf); std::string s(buf, n);
    CHECK(s.find("\"sim\":1") != std::string::npos);
    CHECK(s.find("\"n\":1,\"nc\":0,\"ns\":2") != std::string::npos);              // 1 Ziel (gehalten), nur LD2410C -> nicht bestaetigt
    CHECK(s.find("\"s\":2,\"c\":0,\"a\":1,\"by\":[0,1,0]") != std::string::npos); // HELD, nur LD2410C
    CHECK(s.find("\"r420\"") == std::string::npos);
  }
  // Frontfilter im JSON: Ziel hinter dem Geraet erscheint als Rohwert mit f:0, aber nie als Spur
  { Rig q; int16_t xyv[3][3] = {{100, -800, -30}, {-300, 1800, -30}, {0, 0, 0}}; uint8_t f[30]; encode2450(f, xyv, 2);
    for (int rep = 0; rep < 2; rep++)   // neues Ziel wird erst nach 2 Frames in Folge uebernommen (Ausreisser-Pruefung)
      for (int i = 0; i < 30; i++) if (q.a.feed(f[i])) { q.now = 1000 + rep * 100; q.trk.onFrame2450(q.a.last, q.now); q.l450.note(q.now); }
    q.trk.tick(1100);
    int n = q.json(buf, sizeof buf, false); std::string s(buf, n);
    CHECK(s.find("\"n\":1,") != std::string::npos);
    CHECK(s.find("{\"v\":1,\"x\":100,\"y\":-800,\"sp\":-30,\"res\":360,\"f\":0}") != std::string::npos);
    CHECK(s.find("{\"v\":1,\"x\":-300,\"y\":1800,\"sp\":-30,\"res\":360,\"f\":1}") != std::string::npos);
    CHECK(s.find("\"fd\":2,\"fy\":150,\"fm\":800,\"fa\":60") != std::string::npos);
    CHECK(s.find("\"y\":-800,\"sp\":-30,\"s\":") == std::string::npos); }   // keine Spur bei y=-800
  // Ohne Sensoren: alles "keine Daten"
  { Rig q; int n = q.json(buf, sizeof buf, false); std::string s(buf, n);
    CHECK(balanced(buf, n)); CHECK(s.find("\"s\":[{\"st\":0") != std::string::npos); CHECK(s.find("\"sim\":0") != std::string::npos);
    CHECK(s.find("\"tr\":[]") != std::string::npos); }
  // Neue Felder: Ausreisserzaehler, Geraete-Messwerte, Konfiguration
  { Rig q; q.runTo(3000); int n = q.json(buf, sizeof buf); std::string s(buf, n);
    CHECK(s.find("\"ol\":0,\"pf\":[252,28,100,31],\"cf\":[100,300,1500,2,50]") != std::string::npos); }
  // Sendetakt und Bandbreite: Planer wie in main.cpp (sofort nach jedem LD2450-Frame, min. 25 ms Abstand, sonst alle 50 ms)
  { Rig q; uint32_t lastJson = 0; bool pending = false; int lines = 0; long bytes = 0; uint32_t maxGap = 0, prev = 0; int maxLine = 0;
    for (uint32_t now = 1; now <= 20000; now++) {
      q.now = now;
      if (now % 100 == 0) { pending = true; SimBytes sb; simStep(now, sb);
        for (int i = 0; i < sb.n2450; i++) if (q.a.feed(sb.b2450[i])) { q.trk.onFrame2450(q.a.last, now); q.l450.note(now); }
        for (int i = 0; i < sb.n2410; i++) if (q.b.feed(sb.b2410[i])) { q.trk.onReport2410(q.b.last, now); q.l410.note(now); } }
      q.trk.tick(now);
      if ((pending && now - lastJson >= PC_JSON_MIN_GAP_MS) || now - lastJson >= PC_JSON_PERIOD_MS) {
        lastJson = now; pending = false; int n = q.json(buf, sizeof buf); lines++; bytes += n + 1; if (n + 1 > maxLine) maxLine = n + 1;
        if (prev && now - prev > maxGap) maxGap = now - prev; prev = now; }
    }
    double lps = lines / 20.0, bps = bytes / 20.0;
    printf("  JSON-Takt: %.1f Zeilen/s, laengste Zeile %d Byte, mittlere Last %.0f Byte/s, groesster Abstand %u ms (Limit 460800 Baud = 46080 Byte/s)\n", lps, maxLine, bps, (unsigned)maxGap);
    CHECK(lps >= 19.0 && lps <= 41.0);
    CHECK(maxGap <= (uint32_t)PC_JSON_PERIOD_MS + 1);
    CHECK(lps * maxLine < 46080 * 0.5);                  // Dauerlast mit laengster Zeile < 50 % der UART-Kapazitaet
    CHECK(40.0 * 1100 < 46080); }                         // theoretisches Maximum (min. Abstand 25 ms) mit 1,1-KB-Worst-Case-Zeile bleibt unter der Kapazitaet
  // Zu kleiner Puffer -> -1 (kein stilles Abschneiden)
  { Rig q; q.runTo(3000); char small[100]; CHECK(q.json(small, sizeof small) == -1); }
  // Worst case: 8 Spuren + 2 Orphans + grosse Zahlen -> passt in 3072?
  { Rig q; q.trk.cfg.holdGraceMs = 30000;
    for (int i = 0; i < FUSION_MAX_TRACKS; i++) { Track& t = q.trk.tr[i]; t.used = true; t.id = 99; t.x = -5999; t.y = 5999; t.speed = -9999; t.st = TS_HELD; t.confirmed = true; t.agree = 3; for (int k = 0; k < 3; k++) t.agreeBy[k] = true; }
    q.trk.orphan[1][0].used = q.trk.orphan[1][1].used = true;
    q.a.last.t[0] = {true, -32000, 32000, -32000, 65535}; q.a.last.t[1] = q.a.last.t[0]; q.a.last.t[2] = q.a.last.t[0];
    q.a.frames = q.a.resyncs = q.a.bytes = 4000000000u; q.b.frames = q.b.resyncs = q.b.bytes = 4000000000u;
    q.now = 4000000000u;
    int n = q.json(buf, sizeof buf); printf("  Worst case: %d Byte\n", n);
    CHECK(n > 0 && n < 3000); CHECK(balanced(buf, n)); }
  printf("\n%d Pruefungen, %d fehlgeschlagen\n", checks, fails);
  return fails ? 1 : 0;
}
