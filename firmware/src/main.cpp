// CYD Radar-Tracker: LD2450 + LD2410C Sensorfusion (LD2420 optional per ENABLE_LD2420), nur Ziele vor dem Geraet, Touch-UI. KEIN WLAN, kein Passwort.
// STATUS: kompiliert; Parser/Fusion am PC getestet. Auf Hardware mit echten Sensoren UNGETESTET.
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "ld_parsers.h"
#include "fusion.h"
#include "sim.h"
#include "ui.h"
#include "state_json.h"

TFT_eSPI tft;
TFT_eSprite rg(&tft);     // Seiteninhalt/Radarbereich 240x218 (8 Bit), wird je Frame komplett gepusht
TFT_eSprite bgs(&tft);    // statischer Radar-Hintergrund 240x218 (einmal je Zoomstufe gezeichnet)
TFT_eSprite sbs(&tft);    // Statusleiste 320x22
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

static Ld2450Parser p450;
static Ld2410Parser p410;
#if ENABLE_LD2420
static Ld2420Parser p420;
static SensorLink link420;
#endif
static SensorLink link450, link410;
static Tracker trk;
static UiState ui;
static bool rgOk = false, bgOk = false, sbOk = false;
static int8_t bgZoom = -1;                 // fuer welche Zoomstufe bgs gilt (-1 = ungueltig)
static bool chromeDirty = true;            // Statusleiste + rechte Buttons neu zeichnen
static uint32_t chromeSig = 0xFFFFFFFF, lastChrome = 0;
// Messwerte (am Geraet, siehe Radar-Info links unten und '#'-Zeile)
static volatile bool renderPending = false, jsonPending = false;
static uint32_t frameMs = 0, framePendingMs = 0, perfT0 = 0, perfFrames0 = 0, perfRenders = 0, perfLatSum = 0, perfLatN = 0, perfLatMax = 0, perfRenderUs = 0;
static uint16_t pFps10 = 0, pRenderMs = 0, pFrameHz10 = 0, pLatMs = 0, pLatMax = 0;

#if PC_SERIAL_JSON
static char jbuf[3072];
static char cmdBuf[24]; static uint8_t cmdN = 0;
static uint32_t lastJson = 0, lastInfo = 0, jsonDropped = 0;
#endif
static uint32_t gNow = 0, lastTouchMs = 0, lastRender = 0, lastSim = 0, demoT0 = 0, lastBoot = 0;

// Jeder neue LD2450-Frame wird sofort in die Fusion uebernommen und loest Anzeige + JSON-Zeile aus.
static void feed450(uint8_t b) {
  if (p450.feed(b)) {
    link450.note(gNow); trk.onFrame2450(p450.last, gNow);
    if (!renderPending) { renderPending = true; framePendingMs = gNow; }
    jsonPending = true;
  }
}
static void feed410(uint8_t b) { if (p410.feed(b)) { link410.note(gNow); trk.onReport2410(p410.last, gNow); } }
#if ENABLE_LD2420
static void feed420(uint8_t b) { if (p420.feed(b)) { link420.note(gNow); trk.onReport2420(p420.last, gNow); } }
#endif

static void saveSettings() {
  prefs.putBool("zoom6", ui.zoom6);
  prefs.putBool("mirror", ui.mirrorX);
  prefs.putInt("tol", trk.cfg.tol2410);
  prefs.putUInt("hold", trk.cfg.holdGraceMs);
}

static void setDemo(bool on) {
  ui.demo = on;
  trk.reset();
  link450 = SensorLink(); link410 = SensorLink();
  p450 = Ld2450Parser(); p410 = Ld2410Parser();
#if ENABLE_LD2420
  link420 = SensorLink(); p420 = Ld2420Parser();
#endif
  demoT0 = lastSim = millis();
}

static void handleButton(uint8_t id) {
  switch (id) {
    case B_ZOOM: ui.zoom6 = !ui.zoom6; break;
    case B_PAGE: ui.page = (ui.page + 1) % PG_COUNT; break;
    case B_DEMO: setDemo(!ui.demo); break;
    case B_RESET: trk.reset(); break;
    case B_TOL_M: trk.cfg.tol2410 = constrain(trk.cfg.tol2410 - 100, 300, 2000); trk.cfg.tol2420 = trk.cfg.tol2410; break;
    case B_TOL_P: trk.cfg.tol2410 = constrain(trk.cfg.tol2410 + 100, 300, 2000); trk.cfg.tol2420 = trk.cfg.tol2410; break;
    case B_HOLD_M: trk.cfg.holdGraceMs = constrain((int)trk.cfg.holdGraceMs - 500, 1000, 30000); break;
    case B_HOLD_P: trk.cfg.holdGraceMs = constrain((int)trk.cfg.holdGraceMs + 500, 1000, 30000); break;
    case B_MIRROR: ui.mirrorX = !ui.mirrorX; break;
    case B_DEFAULTS: trk.cfg = FusionCfg(); ui.mirrorX = false; break;
  }
  saveSettings();
  lastRender = 0; chromeDirty = true;   // sofort neu zeichnen
}

#if PC_SERIAL_JSON
// Nie blockieren: nur schreiben, wenn der Sendepuffer die ganze Zeile aufnimmt (sonst Zeile verwerfen).
static void pcEmit(const char* s, int n) {
  if (Serial.availableForWrite() >= n) Serial.write((const uint8_t*)s, n); else jsonDropped++;
}
static void pcHandleCmd(const char* c) {
  if (!strcmp(c, "sim 1")) { if (!ui.demo) { setDemo(true); saveSettings(); } }
  else if (!strcmp(c, "sim 0")) { if (ui.demo) { setDemo(false); saveSettings(); } }
  else if (!strcmp(c, "reset")) trk.reset();
  lastRender = 0; chromeDirty = true;
}
static void pcPoll() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { if (cmdN) { cmdBuf[cmdN] = 0; pcHandleCmd(cmdBuf); cmdN = 0; } }
    else if (cmdN < sizeof(cmdBuf) - 1) cmdBuf[cmdN++] = ch; else cmdN = 0;
  }
  // sofort nach jedem neuen LD2450-Frame (Mindestabstand PC_JSON_MIN_GAP_MS), sonst alle PC_JSON_PERIOD_MS (20 Hz)
  if ((jsonPending && gNow - lastJson >= PC_JSON_MIN_GAP_MS) || gNow - lastJson >= PC_JSON_PERIOD_MS) {
    lastJson = gNow; jsonPending = false;
    StateView sv = {&trk, &p450, &p410, &link450, &link410,
#if ENABLE_LD2420
                    &p420, &link420,
#endif
                    gNow, ui.demo, LINK_TIMEOUT_MS, pFps10, pRenderMs, pFrameHz10, pLatMs};
    int n = buildStateJson(jbuf, sizeof(jbuf) - 2, sv);
    if (n > 0) { jbuf[n++] = '\n'; pcEmit(jbuf, n); }
  }
  if (gNow - lastInfo >= 5000) {
    lastInfo = gNow;
    char info[260]; int k = snprintf(info, sizeof info,
        "# CYD-Tracker-2sensor-v3 self%d front y>%d r>=%d fov%d drop%lu alpha%.2f lost%d jump%d fps%u.%u rend%ums f450 %u.%u/s lat%ums max%ums\n",
        (int)SELF_ZONE_MM, (int)MIN_FRONT_Y_MM, (int)MIN_RANGE_MM, (int)FRONT_HALF_FOV_DEG, (unsigned long)jsonDropped, (double)POS_SMOOTH_ALPHA,
        (int)TRACK_LOST_MS, (int)OUTLIER_JUMP_MM, pFps10 / 10, pFps10 % 10, pRenderMs, pFrameHz10 / 10, pFrameHz10 % 10, pLatMs, pLatMax);
    pcEmit(info, k);
  }
}
#endif

static void fillView(UiView& v) {
  v.trk = &trk; v.p450 = &p450; v.p410 = &p410;
  v.link[0] = &link450; v.link[1] = &link410; v.link[2] = &link410;
#if ENABLE_LD2420
  v.p420 = &p420; v.link[2] = &link420;
#endif
  v.now = gNow; v.ui = &ui;
  v.ls[0] = link450.state(gNow, LINK_TIMEOUT_MS);
  v.ls[1] = link410.state(gNow, LINK_TIMEOUT_MS);
  v.ls[2] = LINK_NONE;
#if ENABLE_LD2420
  v.ls[2] = link420.state(gNow, LINK_TIMEOUT_MS, !p420.last.presence);
#endif
  v.fps10 = pFps10; v.renderMs = pRenderMs; v.frameHz10 = pFrameHz10; v.latMs = pLatMs;
}

// Zeichnet NUR den Radar-/Seitenbereich (240x218) jeden Frame; Statusleiste und rechte Buttons nur bei Aenderung bzw. alle UI_STATUS_MS.
static void render() {
  uint32_t t0 = micros();
  UiView v; fillView(v);
  uint32_t sig = (uint32_t)v.ls[0] | ((uint32_t)v.ls[1] << 2) | ((uint32_t)v.ls[2] << 4) | ((uint32_t)ui.demo << 6) | ((uint32_t)ui.page << 7) |
                 ((uint32_t)ui.zoom6 << 9) | ((uint32_t)ui.mirrorX << 10);
  bool full = chromeDirty || sig != chromeSig;
  if (!rgOk) {                                   // Notfall ohne Sprite: alles direkt (langsam)
    uiDrawAll(tft, v);
  } else {
    if (full || gNow - lastChrome >= UI_STATUS_MS) {
      if (sbOk) { uiDrawStatusOnly(sbs, v); sbs.pushSprite(0, 0); } else uiDrawStatusOnly(tft, v);
      lastChrome = gNow;
    }
    if (full) { uiDrawRightColumn(tft, v); chromeSig = sig; chromeDirty = false; }
    if (ui.page == PG_RADAR) {
      int8_t zk = ui.zoom6 ? 1 : 0;
      if (bgOk) {
        if (bgZoom != zk) { uiDrawRegionBg(bgs, v); bgZoom = zk; }
        memcpy(rg.getPointer(), bgs.getPointer(), (size_t)UI_REGION_W * UI_REGION_H);   // Hintergrund kopieren statt neu zeichnen
      } else uiDrawRegionBg(rg, v);
    }
    uiDrawRegionFg(rg, v);
    rg.pushSprite(0, UI_REGION_Y);
  }
  uint32_t t1 = micros();
  // Messwerte: Zeichenzeit, Latenz (neuer LD2450-Frame geparst -> Anzeige fertig), fps
  perfRenders++; perfRenderUs += t1 - t0;
  if (framePendingMs) { uint32_t l = millis() - framePendingMs; perfLatSum += l; perfLatN++; if (l > perfLatMax) perfLatMax = l; framePendingMs = 0; }
  uint32_t dtp = millis() - perfT0;
  if (dtp >= 1000) {
    pFps10 = (uint16_t)(perfRenders * 10000UL / dtp);
    pRenderMs = (uint16_t)(perfRenders ? (perfRenderUs / perfRenders + 500) / 1000 : 0);
    pFrameHz10 = (uint16_t)((p450.frames - perfFrames0) * 10000UL / dtp);
    pLatMs = (uint16_t)(perfLatN ? perfLatSum / perfLatN : 0); pLatMax = (uint16_t)perfLatMax;
    perfT0 = millis(); perfFrames0 = p450.frames; perfRenders = perfRenderUs = perfLatSum = perfLatN = perfLatMax = 0;
  }
}

void setup() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
#if PC_SERIAL_JSON
  Serial.setTxBufferSize(2048);
  Serial.begin(PC_SERIAL_BAUD);          // USB-Serial (CH340) zum PC: JSON-Zeilen, kein Debug-Text
#endif
  // Nur RX-Pins (Sensor-TX -> ESP-RX); TX = -1: ESP sendet nichts an die Sensoren (Standardmodus lesen).
  Serial2.setRxBufferSize(1024); Serial2.begin(BAUD_LD2450, SERIAL_8N1, PIN_RX_LD2450, -1);
  Serial1.setRxBufferSize(1024); Serial1.begin(BAUD_LD2410, SERIAL_8N1, PIN_RX_LD2410, -1);
#if ENABLE_LD2420
  // UART0: RX auf IO35 umgemappt, TX bleibt IO1 (Boot-Log). USB-Seriell-Eingang ist damit deaktiviert.
  // UNTESTED: UART0-RX-Umlegung auf IO35 (nicht auf Hardware geprüft)
  Serial.setRxBufferSize(1024); Serial.begin(BAUD_LD2420, SERIAL_8N1, PIN_RX_LD2420, 1);
#endif   // ohne ENABLE_LD2420 bleiben IO35 und UART0 unbelegt

  prefs.begin("trk", false);
  ui.zoom6 = prefs.getBool("zoom6", true);
  ui.mirrorX = prefs.getBool("mirror", false);
  trk.cfg.tol2410 = trk.cfg.tol2420 = constrain(prefs.getInt("tol", FUSION_TOL_2410_MM), 300, 2000);
  trk.cfg.holdGraceMs = constrain((int)prefs.getUInt("hold", FUSION_HOLD_GRACE_MS), 1000, 30000);

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(DISPLAY_ROTATION);

  rg.setColorDepth(8);  rgOk = rg.createSprite(UI_REGION_W, UI_REGION_H) != nullptr;
  bgs.setColorDepth(8); bgOk = rgOk && bgs.createSprite(UI_REGION_W, UI_REGION_H) != nullptr;
  sbs.setColorDepth(8); sbOk = sbs.createSprite(320, 22) != nullptr;
  perfT0 = millis();
  gNow = millis();
  render();
}

void loop() {
  gNow = millis();

  if (!ui.demo) {
    int n = 0;
    while (Serial2.available() && n++ < 512) feed450((uint8_t)Serial2.read());
    n = 0;
    while (Serial1.available() && n++ < 512) feed410((uint8_t)Serial1.read());
    n = 0;
#if ENABLE_LD2420
    while (Serial.available() && n++ < 512) feed420((uint8_t)Serial.read());
#endif
  } else {
    while (Serial2.available()) Serial2.read();   // echte Daten im Demo-Modus verwerfen
    while (Serial1.available()) Serial1.read();
#if ENABLE_LD2420
    while (Serial.available()) Serial.read();
#endif
    while (gNow - lastSim >= 100) {               // Simulation in 100-ms-Schritten, gleiche Parser + Fusion
      lastSim += 100;
      SimBytes sb;
      simStep(lastSim - demoT0, sb);
      for (int i = 0; i < sb.n2450; i++) feed450(sb.b2450[i]);
      for (int i = 0; i < sb.n2410; i++) feed410(sb.b2410[i]);
#if ENABLE_LD2420
      for (int i = 0; i < sb.n2420; i++) feed420((uint8_t)sb.b2420[i]);
#endif
    }
  }

  trk.tick(gNow);   // jede Schleife (Timeouts TRACK_LOST_MS exakt, Rechenzeit vernachlaessigbar)
#if PC_SERIAL_JSON
  pcPoll();
#endif

  if (ts.tirqTouched() && ts.touched() && gNow - lastTouchMs > TOUCH_DEBOUNCE_MS) {
    TS_Point p = ts.getPoint();
    int x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319), 0, 319);
    int y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239), 0, 239);
    lastTouchMs = gNow;
    Btn b[10]; int n = uiButtons(ui, trk, b);
    for (int i = 0; i < n; i++)
      if (x >= b[i].x && x < b[i].x + b[i].w && y >= b[i].y && y < b[i].y + b[i].h) { handleButton(b[i].id); break; }
  }

  if (digitalRead(PIN_BOOT_BTN) == LOW && gNow - lastBoot > 600) { lastBoot = gNow; handleButton(B_DEMO); }

  // Radar: sofort nach jedem neuen Frame (frueestens nach UI_MIN_FRAME_MS), sonst mindestens alle UI_RENDER_MS. Andere Seiten: 5 Hz.
  uint32_t dr = gNow - lastRender;
  bool due = (ui.page == PG_RADAR) ? ((renderPending && dr >= UI_MIN_FRAME_MS) || dr >= UI_RENDER_MS) : (dr >= 200);
  if (due) { lastRender = gNow; renderPending = false; render(); }
}
