// Demo-/Simulationsmodus + Frame-Encoder (auch fuer den Host-Test).
// Die Simulation erzeugt ECHTE Bytefolgen im Sensorformat und laeuft durch dieselben
// Parser und dieselbe Fusion wie reale Sensordaten.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ld_parsers.h"

static const uint32_t SIM_PERIOD_MS = 40000;

// ---- Encoder (Gegenstueck zu den Parsern) ----
static inline uint16_t simEncSM(int v) { return v >= 0 ? (uint16_t)(0x8000 | (v & 0x7FFF)) : (uint16_t)((-v) & 0x7FFF); }

// out: 30 Byte. x,y mm; speed cm/s; n<=3 Ziele; fehlende = 0
static inline void encode2450(uint8_t* out, const int16_t (*xyv)[3], int n) {
  static const uint8_t H[4] = {0xAA, 0xFF, 0x03, 0x00};
  memcpy(out, H, 4);
  for (int i = 0; i < 3; i++) {
    uint8_t* p = out + 4 + i * 8;
    int x = 0, y = 0, v = 0, res = 0;
    if (i < n) { x = xyv[i][0]; y = xyv[i][1]; v = xyv[i][2]; res = 360; }
    uint16_t a = (x == 0 && y == 0) ? 0 : simEncSM(x), b = (x == 0 && y == 0) ? 0 : simEncSM(y), c = (x == 0 && y == 0) ? 0 : simEncSM(v);
    p[0] = a & 255; p[1] = a >> 8; p[2] = b & 255; p[3] = b >> 8; p[4] = c & 255; p[5] = c >> 8;
    p[6] = res & 255; p[7] = res >> 8;
  }
  out[28] = 0x55; out[29] = 0xCC;
}
// out: 23 Byte (Normalmodus)
static inline int encode2410(uint8_t* out, uint8_t state, uint16_t moveCm, uint8_t moveEn, uint16_t stillCm, uint8_t stillEn, uint16_t detCm) {
  static const uint8_t H[4] = {0xF4, 0xF3, 0xF2, 0xF1}, F[4] = {0xF8, 0xF7, 0xF6, 0xF5};
  memcpy(out, H, 4); out[4] = 0x0D; out[5] = 0x00;
  uint8_t* p = out + 6;
  p[0] = 0x02; p[1] = 0xAA; p[2] = state;
  p[3] = moveCm & 255; p[4] = moveCm >> 8; p[5] = moveEn;
  p[6] = stillCm & 255; p[7] = stillCm >> 8; p[8] = stillEn;
  p[9] = detCm & 255; p[10] = detCm >> 8; p[11] = 0x55; p[12] = 0x00;
  memcpy(out + 19, F, 4);
  return 23;
}

// ---- Szenario (Bodenwahrheit) ----
struct SimPerson { bool present; float x, y; bool moving; bool seen2450; };
static inline float simLerp(float a, float b, float f) { return a + (b - a) * (f < 0 ? 0 : f > 1 ? 1 : f); }

// Person A: laeuft rein (0-6 s), steht (6-22 s; LD2450 verliert sie ab 8,5 s), laeuft weiter (22-28 s), geht raus (28-32 s)
// Person B: laeuft durchs Bild (12-24 s)
static inline void simScenario(uint32_t tms, SimPerson& A, SimPerson& B) {
  float t = (tms % SIM_PERIOD_MS) / 1000.0f;
  A = {true, 0, 0, false, true}; B = {false, 0, 0, true, true};
  if (t < 6)        { A.x = simLerp(-1500, 600, t / 6);  A.y = simLerp(4500, 2500, t / 6); A.moving = true; }
  else if (t < 22)  { A.x = 600; A.y = 2500; A.moving = false; A.seen2450 = (t < 8.5f); }
  else if (t < 28)  { A.x = simLerp(600, -500, (t - 22) / 6); A.y = simLerp(2500, 1800, (t - 22) / 6); A.moving = true; }
  else if (t < 32)  { A.x = simLerp(-500, -3500, (t - 28) / 4); A.y = simLerp(1800, 800, (t - 28) / 4); A.moving = true; }
  else A.present = false;
  if (t >= 12 && t < 24) { B.present = true; B.x = simLerp(-1200, 2600, (t - 12) / 12); B.y = simLerp(1500, 5600, (t - 12) / 12); }
}

// Radialgeschwindigkeit in cm/s grob aus Bewegung (nur Vorzeichen/Betrag fuer die Anzeige)
static inline int simSpeed(const SimPerson& p) { return p.moving ? -35 : 0; }

static inline bool simInFov(float x, float y) {
  if (y <= 0 || y > 6000) return false;
  return fabsf(x) <= y * 1.732f;   // +-60 Grad
}

// Erzeugt die Sensor-Bytes fuer einen 100-ms-Schritt. Rueckgabe: Anzahl Bytes je Sensor.
struct SimBytes {
  uint8_t b2450[30]; int n2450;
  uint8_t b2410[32]; int n2410;
  char b2420[40];    int n2420;
};
static inline void simStep(uint32_t tms, SimBytes& o) {
  SimPerson A, B; simScenario(tms, A, B);
  int16_t xyv[3][3]; int n = 0;
  const SimPerson* ps[2] = {&A, &B};
  for (int i = 0; i < 2; i++) {
    const SimPerson& p = *ps[i];
    if (p.present && p.seen2450 && simInFov(p.x, p.y) && n < 3) {
      xyv[n][0] = (int16_t)p.x; xyv[n][1] = (int16_t)p.y; xyv[n][2] = (int16_t)simSpeed(p); n++;
    }
  }
  encode2450(o.b2450, xyv, n); o.n2450 = 30;
  // 2410C: bewegte Person -> Bewegt-Distanz, stehende -> Still-Distanz (Gate-Rauschen +-20 cm)
  uint8_t st = 0; uint16_t mv = 0, sd = 0;
  float jit = 20.0f * sinf(tms / 700.0f);
  for (int i = 0; i < 2; i++) {
    const SimPerson& p = *ps[i];
    if (!p.present || !simInFov(p.x, p.y)) continue;
    uint16_t cm = (uint16_t)(sqrtf(p.x * p.x + p.y * p.y) / 10.0f + jit);
    if (p.moving) { if (!mv) { st |= 1; mv = cm; } } else { if (!sd) { st |= 2; sd = cm; } }
  }
  o.n2410 = encode2410(o.b2410, st, mv, mv ? 60 : 0, sd, sd ? 45 : 0, st ? (mv ? mv : sd) : 0);
  // 2420: eine Distanz = naechste Person
  float best = 1e9f; bool any = false;
  for (int i = 0; i < 2; i++) {
    const SimPerson& p = *ps[i];
    if (!p.present || !simInFov(p.x, p.y)) continue;
    float r = sqrtf(p.x * p.x + p.y * p.y); if (r < best) best = r; any = true;
  }
  if (any) o.n2420 = snprintf(o.b2420, sizeof(o.b2420), "ON\r\nRange %d\r\n", (int)(best / 10.0f - jit));
  else     o.n2420 = snprintf(o.b2420, sizeof(o.b2420), "OFF\r\n");
}
