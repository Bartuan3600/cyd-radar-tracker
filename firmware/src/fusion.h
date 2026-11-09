// Sensor-Fusion + Tracking: LD2450 (Position, einzige Quelle fuer Spuren) + LD2410C (optional LD2420) (Praesenz in Ruhe, Bestaetigung).
// Spuren entstehen NUR aus LD2450-Zielen im Frontbereich (siehe MIN_FRONT_Y_MM/MIN_RANGE_MM/FRONT_HALF_FOV_DEG in config.h).
// Reiner C++-Code ohne Arduino-Abhaengigkeit (auf dem PC getestet).
// ===================== FUSIONS-PARAMETER (hier anpassen) =====================
#include "../include/config.h"
#define FUSION_MAX_TRACKS        8      // gleichzeitige Spuren (3 live + gehaltene)
#define FUSION_ASSOC_GATE_MM     OUTLIER_JUMP_MM  // max. Abstand Messung <-> Spur fuer gleiche ID (mm); weiter entfernt = Sprung, erst nach Bestaetigung
#define FUSION_SMOOTH_ALPHA      POS_SMOOTH_ALPHA // 1.0 = ungeglaettet (Standard), <1 = EMA
#define FUSION_MOVE_SPEED_CMS    1      // |Speed| >= Wert (cm/s) -> "bewegt" (LD2450-Rohwert)
#define FUSION_STILL_AFTER_MS    1500   // so lange ohne Bewegung -> Ziel gilt als "ruhend"
#define FUSION_LOST_MS           TRACK_LOST_MS // LD2450 sieht Spur so lange nicht -> verloren (entfernt, ausser ruhend + LD2410C bestaetigt -> gehalten)
#define FUSION_TOL_2410_MM       900    // Distanz-Toleranz LD2410C (Aufloesung 0,75 m je Gate)
#define FUSION_TOL_2420_MM       900    // Distanz-Toleranz LD2420 (Gate 0,7 m, Genauigkeit ~ +-0,35 m)
#define FUSION_HOLD_GRACE_MS     3000   // gehaltener Punkt bleibt, solange letzte 2410/2420-Uebereinstimmung < Wert alt
#define FUSION_HOLD_MAX_MS       600000 // absolute Obergrenze fuer Halten (10 min)
#define FUSION_AGREE_WINDOW_MS   1500   // Sensor zaehlt als "uebereinstimmend", wenn letzter Treffer < Wert alt
#define FUSION_CONFIRM_MIN       2      // "bestaetigt" = mindestens N uebereinstimmende Sensoren
#define FUSION_ORPHAN_MS         1500   // Praesenz ohne Spur so lange als Bogen anzeigen (nur SHOW_RANGE_ONLY_RINGS=1)
#define CAND_MAX_AGE_MS          250    // Kandidat gilt nur fuer den naechsten Frame (Frame-Periode ~100 ms, 1 verlorener Frame toleriert)
#define FUSION_NSENS             (ENABLE_LD2420 ? 3 : 2)   // aktive Sensoren (LD2450, LD2410C, [LD2420])
// ============================================================================
#pragma once
#include <stdint.h>
#include <math.h>
#include "ld_parsers.h"

enum SensorId { S2450 = 0, S2410 = 1, S2420 = 2 };
enum TrackState : uint8_t { TS_MOVING = 0, TS_STILL = 1, TS_HELD = 2 };

struct FusionCfg {
  int tol2410 = FUSION_TOL_2410_MM;
  int tol2420 = FUSION_TOL_2420_MM;
  uint32_t holdGraceMs = FUSION_HOLD_GRACE_MS;
};

struct Track {
  bool used = false;
  uint8_t id = 0;
  float x = 0, y = 0;                 // mm, geglaettet (x rechts positiv, y nach vorn)
  int16_t speed = 0;                  // cm/s, letzter Rohwert
  TrackState st = TS_MOVING;
  bool confirmed = false;
  uint8_t agree = 0;                  // Anzahl uebereinstimmender Sensoren
  bool agreeBy[3] = {false, false, false};
  uint32_t born = 0, lastSeen450 = 0, lastMoving = 0, lostAt = 0;
  uint32_t lastMatch[3] = {0, 0, 0};
  bool ever[3] = {false, false, false};
  float range() const { return sqrtf(x * x + y * y); }
};

struct Cand { bool used = false; float x = 0, y = 0; int16_t speed = 0; uint32_t t = 0; };  // Messung ohne Spur, wartet auf Bestaetigung im naechsten Frame

struct Orphan { bool used = false; uint16_t distMm = 0; uint32_t t = 0; };  // Praesenz ohne passende Spur

class Tracker {
 public:
  Track tr[FUSION_MAX_TRACKS];
  Cand cand[3];                       // unbestaetigte neue Ziele / Spruenge (hoechstens 3 pro Frame)
  Orphan orphan[3][2];                // [Sensor][Slot], nur Sensor 1/2 genutzt
  FusionCfg cfg;
  uint32_t created = 0, removed = 0;
  uint32_t outliers = 0;              // einzelne unplausible Messungen (neues Ziel/Sprung, im naechsten Frame nicht bestaetigt) -> verworfen
  uint32_t droppedFront = 0;          // LD2450-Ziele, die der Frontfilter verworfen hat (hinter/zu nah/ausserhalb Kegel)

  // Frontfilter: true = Ziel liegt vor dem Geraet im Erfassungskegel und ausserhalb der Blindzone
  static bool frontOk(float x, float y) {
    if (y <= (float)MIN_FRONT_Y_MM) return false;
    if (sqrtf(x * x + y * y) < (float)MIN_RANGE_MM) return false;
    if (fabsf(atan2f(x, y)) * 57.2957795f > (float)FRONT_HALF_FOV_DEG) return false;
    return true;
  }

  void reset() { for (auto& t : tr) t.used = false; for (auto& c : cand) c.used = false; for (auto& s : orphan) for (auto& o : s) o.used = false; }

  int count() const { int n = 0; for (auto& t : tr) n += t.used; return n; }
  int countConfirmed() const { int n = 0; for (auto& t : tr) n += (t.used && t.confirmed); return n; }

  // ---- LD2450-Frame: Positionen, Zuordnung, Glaettung ----
  void onFrame2450(const Frame2450& f, uint32_t now) {
    const Target2450* m[3]; int nm = 0;
    for (int i = 0; i < 3; i++) if (f.t[i].valid) {
      if (frontOk(f.t[i].x, f.t[i].y)) m[nm++] = &f.t[i]; else droppedFront++;
    }
    bool mUsed[3] = {false, false, false};
    bool tUsed[FUSION_MAX_TRACKS] = {};
    // gierige Zuordnung nach kleinstem Abstand
    for (;;) {
      float best = (float)FUSION_ASSOC_GATE_MM; int bi = -1, bj = -1;
      for (int i = 0; i < nm; i++) if (!mUsed[i])
        for (int j = 0; j < FUSION_MAX_TRACKS; j++) if (tr[j].used && !tUsed[j]) {
          float d = hypotf(m[i]->x - tr[j].x, m[i]->y - tr[j].y);
          if (d < best) { best = d; bi = i; bj = j; }
        }
      if (bi < 0) break;
      mUsed[bi] = true; tUsed[bj] = true;
      update_(tr[bj], *m[bi], now);
    }
    // Unzugeordnete Messungen (neues Ziel oder Sprung > OUTLIER_JUMP_MM): erst uebernehmen, wenn der vorige Frame
    // eine Messung am (fast) gleichen Ort hatte; sonst nur merken (Ausreisser/Rauschen wird so nie angezeigt).
    Cand next[3]; int nn = 0;
    for (int i = 0; i < nm; i++) if (!mUsed[i]) {
#if SPAWN_CONFIRM_FRAMES >= 2
      int ci = -1; float best = (float)CAND_GATE_MM;
      for (int c = 0; c < 3; c++) if (cand[c].used && age_(now, cand[c].t) <= CAND_MAX_AGE_MS) {
        float d = hypotf(m[i]->x - cand[c].x, m[i]->y - cand[c].y);
        if (d < best) { best = d; ci = c; }
      }
      if (ci >= 0) { cand[ci].used = false; spawn_(*m[i], now); }
      else { next[nn].used = true; next[nn].x = m[i]->x; next[nn].y = m[i]->y; next[nn].speed = m[i]->speed; next[nn].t = now; nn++; }
#else
      spawn_(*m[i], now);
#endif
    }
    for (int c = 0; c < 3; c++) if (cand[c].used) outliers++;   // Kandidat des Vorframes blieb unbestaetigt
    for (int c = 0; c < 3; c++) cand[c] = (c < nn) ? next[c] : Cand();
  }

  // ---- LD2410C: Praesenz-Distanzen (cm) ----
  void onReport2410(const Report2410& r, uint32_t now) {
    clearOrphans_(S2410);
    int slot = 0;
    if ((r.state & 1) && r.moveDist > 0) slow_(S2410, r.moveDist * 10, cfg.tol2410, now, slot);
    if ((r.state & 2) && r.stillDist > 0) slow_(S2410, r.stillDist * 10, cfg.tol2410, now, slot);
  }

#if ENABLE_LD2420
  // ---- LD2420 (optional): Praesenz + Distanz (cm) ----
  void onReport2420(const Report2420& r, uint32_t now) {
    clearOrphans_(S2420);
    int slot = 0;
    if (r.presence && r.rangeValid && r.range > 0) slow_(S2420, r.range * 10, cfg.tol2420, now, slot);
  }
#endif

  // ---- zyklisch aufrufen (z.B. 20-50x pro s): Timeouts, Bestaetigung ----
  void tick(uint32_t now) {
    for (auto& t : tr) {
      if (!t.used) continue;
      uint32_t lostMs = (t.range() > (float)FAR_RANGE_MM) ? (uint32_t)TRACK_LOST_FAR_MS : (uint32_t)FUSION_LOST_MS;
      if (t.st != TS_HELD && age_(now, t.lastSeen450) > lostMs) {
        // Nicht mehr gemeldet: sofort weg. Nur ein ruhendes Ziel, das der LD2410C gerade bestaetigt, wird gehalten.
        bool still = (t.st == TS_STILL) && t.range() >= (float)SELF_ZONE_MM;   // Nahzone (Nutzer hinten/Rueckkeule) wird nie gehalten
        bool lsOk = false;
        for (int s = 1; s < FUSION_NSENS; s++) if (t.ever[s] && age_(now, t.lastMatch[s]) < FUSION_AGREE_WINDOW_MS) lsOk = true;
        if (still && lsOk) { t.st = TS_HELD; t.lostAt = t.lastSeen450; }
        else { t.used = false; removed++; continue; }
      }
      if (t.st == TS_HELD) {
        bool alive = false;
        for (int s = 1; s < FUSION_NSENS; s++)
          if (t.ever[s] && age_(now, t.lastMatch[s]) < cfg.holdGraceMs) alive = true;
        if (!alive || age_(now, t.lostAt) > FUSION_HOLD_MAX_MS) { t.used = false; removed++; continue; }
      }
      t.agree = 0;
      for (int s = 0; s < FUSION_NSENS; s++) {
        t.agreeBy[s] = t.ever[s] && age_(now, t.lastMatch[s]) < FUSION_AGREE_WINDOW_MS;
        t.agree += t.agreeBy[s];
      }
      t.confirmed = t.agree >= FUSION_CONFIRM_MIN;
    }
    for (auto& s : orphan) for (auto& o : s) if (o.used && age_(now, o.t) > FUSION_ORPHAN_MS) o.used = false;
  }

 private:
  uint8_t nextId_ = 1;
  static uint32_t age_(uint32_t now, uint32_t t) { return now - t; }
  void clearOrphans_(int s) { for (auto& o : orphan[s]) o.used = false; }

  void update_(Track& t, const Target2450& m, uint32_t now) {
if (FUSION_SMOOTH_ALPHA >= 1.0f) { t.x = m.x; t.y = m.y; }   // genau die gemessene Position (mm), keine Glaettung
    else { t.x += FUSION_SMOOTH_ALPHA * (m.x - t.x); t.y += FUSION_SMOOTH_ALPHA * (m.y - t.y); }
    t.speed = m.speed;
    t.lastSeen450 = now; t.lastMatch[S2450] = now; t.ever[S2450] = true;
    if (abs((int)m.speed) >= FUSION_MOVE_SPEED_CMS) t.lastMoving = now;
    t.st = (age_(now, t.lastMoving) < FUSION_STILL_AFTER_MS) ? TS_MOVING : TS_STILL;
  }
  void spawn_(const Target2450& m, uint32_t now) {
    int slot = -1;
    for (int j = 0; j < FUSION_MAX_TRACKS; j++) if (!tr[j].used) { slot = j; break; }
    if (slot < 0) {  // voll: aeltesten gehaltenen Punkt opfern
      uint32_t oldest = 0;
      for (int j = 0; j < FUSION_MAX_TRACKS; j++)
        if (tr[j].st == TS_HELD && (slot < 0 || age_(now, tr[j].lostAt) > oldest)) { slot = j; oldest = age_(now, tr[j].lostAt); }
      if (slot < 0) return;
    }
    Track t; t.used = true; t.id = nextId_; nextId_ = (nextId_ >= 99) ? 1 : nextId_ + 1;
    t.x = m.x; t.y = m.y; t.speed = m.speed; t.born = now;
    t.lastSeen450 = now; t.lastMatch[S2450] = now; t.ever[S2450] = true;
    bool mv = abs((int)m.speed) >= FUSION_MOVE_SPEED_CMS;
    t.lastMoving = mv ? now : now - FUSION_STILL_AFTER_MS;
    t.st = mv ? TS_MOVING : TS_STILL;
    tr[slot] = t; created++;
  }
  // eine Praesenz-Distanz des langsamen Sensors der naechstliegenden Spur zuordnen
  void slow_(int sensor, int distMm, int tolMm, uint32_t now, int& slot) {
    if (distMm < SELF_ZONE_MM) return;   // Meldung in der Nahzone = (fast sicher) der Nutzer selbst: darf keine Spur bestaetigen/halten
    int bj = -1; float best = (float)tolMm + 1;
    for (int j = 0; j < FUSION_MAX_TRACKS; j++) if (tr[j].used) {
      float d = fabsf(tr[j].range() - (float)distMm);
      if (d <= (float)tolMm && d < best) { best = d; bj = j; }
    }
    if (bj >= 0) { tr[bj].lastMatch[sensor] = now; tr[bj].ever[sensor] = true; }
    else if (SHOW_RANGE_ONLY_RINGS && slot < 2) { Orphan& o = orphan[sensor][slot++]; o.used = true; o.distMm = (uint16_t)distMm; o.t = now; }
  }
};

// ---- Verbindungsstatus eines Sensors (gruen = Daten, rot = keine) ----
enum LinkState : uint8_t { LINK_NONE = 0, LINK_OK = 1, LINK_IDLE = 2 };
struct SensorLink {
  uint32_t lastMs = 0; bool ever = false; uint32_t frames = 0;
  void note(uint32_t now) { lastMs = now; ever = true; frames++; }
  // quietOkWhenOff: LD2420 sendet bei "OFF" evtl. nur selten -> gelb statt rot, wenn zuvor Daten kamen und Praesenz aus
  LinkState state(uint32_t now, uint32_t timeoutMs, bool quietIsIdle = false) const {
    if (ever && now - lastMs < timeoutMs) return LINK_OK;
    if (ever && quietIsIdle) return LINK_IDLE;
    return LINK_NONE;
  }
};
