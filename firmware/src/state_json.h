// JSON-Zeile fuer den PC-Viewer (USB-Serial des CYD, Format wie firmware-stm32/src/state_json.h, aber nur 2 Sensoren
// [LD2450, LD2410C]; mit ENABLE_LD2420=1 ein dritter). Frontfilter ist schon angewendet. Zusatzfelder:
//   ns = Anzahl Sensoren, fd = vom Frontfilter verworfene LD2450-Ziele, fy/fm/fa = MIN_FRONT_Y_MM / MIN_RANGE_MM / FRONT_HALF_FOV_DEG,
//   ol = verworfene Ausreisser, pf = [Radar-fps*10, Zeichenzeit ms, LD2450-Frames/s*10, Latenz Frame->Display ms] (0 = unbekannt),
//   cf = [POS_SMOOTH_ALPHA*100, TRACK_LOST_MS, OUTLIER_JUMP_MM, SPAWN_CONFIRM_FRAMES, PC_JSON_PERIOD_MS],
//   r450[i].f = 1 wenn das Rohziel im Frontbereich liegt (0 = wird verworfen, nie ein Track).
// Reiner C++-Code, am PC getestet. Kein Heap, feste Puffergroesse.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "ld_parsers.h"
#include "fusion.h"

struct StateView {
  const Tracker* trk;
  const Ld2450Parser* p450; const Ld2410Parser* p410;
  const SensorLink* l450; const SensorLink* l410;
#if ENABLE_LD2420
  const Ld2420Parser* p420; const SensorLink* l420;
#endif
  uint32_t now; bool sim; uint32_t linkTimeoutMs;
  uint16_t fps10, renderMs, frameHz10, latMs;   // Messwerte am Geraet (0 = unbekannt); bei Aggregat-Init automatisch 0
};

struct JsonOut {
  char* b; int cap, n; bool over;
  void add(const char* fmt, ...) {
    if (over) return;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(b + n, (size_t)(cap - n), fmt, ap);
    va_end(ap);
    if (k < 0 || k >= cap - n) { over = true; return; }
    n += k;
  }
};

// Rueckgabe: Laenge, oder -1 bei Pufferueberlauf (dann ist der Inhalt ungueltig).
static inline int buildStateJson(char* buf, int cap, const StateView& v) {
  JsonOut o = {buf, cap, 0, false};
  const int NS = FUSION_NSENS;
  const SensorLink* L[3] = {v.l450, v.l410, v.l410};
  uint32_t fr[3] = {v.p450->frames, v.p410->frames, 0}, rs[3] = {v.p450->resyncs, v.p410->resyncs, 0};
  uint32_t by[3] = {v.p450->bytes, v.p410->bytes, 0};
#if ENABLE_LD2420
  L[2] = v.l420; fr[2] = v.p420->lines; rs[2] = v.p420->resyncs; by[2] = v.p420->bytes;
#endif
  o.add("{\"t\":%lu,\"sim\":%d,\"tol\":%d,\"hold\":%lu,\"n\":%d,\"nc\":%d,\"ns\":%d,\"fd\":%lu,\"fy\":%d,\"fm\":%d,\"fa\":%d,\"ol\":%lu,\"pf\":[%u,%u,%u,%u],\"cf\":[%d,%d,%d,%d,%d],\"s\":[", (unsigned long)v.now, v.sim ? 1 : 0,
        v.trk->cfg.tol2410, (unsigned long)v.trk->cfg.holdGraceMs, v.trk->count(), v.trk->countConfirmed(), NS,
        (unsigned long)v.trk->droppedFront, (int)MIN_FRONT_Y_MM, (int)MIN_RANGE_MM, (int)FRONT_HALF_FOV_DEG,
        (unsigned long)v.trk->outliers, (unsigned)v.fps10, (unsigned)v.renderMs, (unsigned)v.frameHz10, (unsigned)v.latMs,
        (int)lroundf(POS_SMOOTH_ALPHA * 100.0f), (int)TRACK_LOST_MS, (int)OUTLIER_JUMP_MM, (int)SPAWN_CONFIRM_FRAMES, (int)PC_JSON_PERIOD_MS);
  for (int i = 0; i < NS; i++) {
    bool quiet = false;
#if ENABLE_LD2420
    quiet = (i == 2) && !v.p420->last.presence;
#endif
    LinkState ls = L[i]->state(v.now, v.linkTimeoutMs, quiet);
    o.add("%s{\"st\":%d,\"fr\":%lu,\"rs\":%lu,\"by\":%lu,\"age\":%ld}", i ? "," : "", (int)ls, (unsigned long)fr[i],
          (unsigned long)rs[i], (unsigned long)by[i], L[i]->ever ? (long)(v.now - L[i]->lastMs) : -1L);
  }
  o.add("],\"r450\":[");
  for (int i = 0; i < 3; i++) {
    const Target2450& t = v.p450->last.t[i];
    o.add("%s{\"v\":%d,\"x\":%d,\"y\":%d,\"sp\":%d,\"res\":%u,\"f\":%d}", i ? "," : "", t.valid ? 1 : 0, t.x, t.y, t.speed, (unsigned)t.res,
          (t.valid && Tracker::frontOk((float)t.x, (float)t.y)) ? 1 : 0);
  }
  const Report2410& r = v.p410->last;
  o.add("],\"r410\":{\"state\":%u,\"md\":%u,\"me\":%u,\"sd\":%u,\"se\":%u,\"dd\":%u,\"eng\":%d}",
        (unsigned)r.state, (unsigned)r.moveDist, (unsigned)r.moveEnergy, (unsigned)r.stillDist, (unsigned)r.stillEnergy,
        (unsigned)r.detDist, r.engineering ? 1 : 0);
#if ENABLE_LD2420
  { const Report2420& q = v.p420->last;
    o.add(",\"r420\":{\"on\":%d,\"rv\":%d,\"rg\":%u,\"en\":%d}", q.presence ? 1 : 0, q.rangeValid ? 1 : 0, (unsigned)q.range, q.fromEnergyFrame ? 1 : 0); }
#endif
  o.add(",\"tr\":[");
  bool first = true;
  for (int i = 0; i < FUSION_MAX_TRACKS; i++) {
    const Track& t = v.trk->tr[i];
    if (!t.used) continue;
    o.add("%s{\"id\":%d,\"x\":%d,\"y\":%d,\"sp\":%d,\"s\":%d,\"c\":%d,\"a\":%d,\"by\":[%d,%d,%d]}", first ? "" : ",", (int)t.id,
          (int)t.x, (int)t.y, (int)t.speed, (int)t.st, t.confirmed ? 1 : 0, (int)t.agree, t.agreeBy[0] ? 1 : 0,
          t.agreeBy[1] ? 1 : 0, t.agreeBy[2] ? 1 : 0);
    first = false;
  }
  o.add("],\"or\":[");
  first = true;
  for (int s = 1; s < NS; s++)
    for (int k = 0; k < 2; k++) {
      const Orphan& p = v.trk->orphan[s][k];
      if (!p.used) continue;
      o.add("%s{\"s\":%d,\"d\":%u}", first ? "" : ",", s, (unsigned)p.distMm);
      first = false;
    }
  o.add("]}");
  return o.over ? -1 : o.n;
}
