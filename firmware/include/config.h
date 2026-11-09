// Radar-Tracker (CYD ESP32-2432S028R): Pins, Display, Touch. Kein WLAN, kein Passwort.
#pragma once

// ---- Optionaler dritter Sensor: LD2420 (Standard AUS: nicht vorhanden) ----
// 1 = LD2420 an IO35 (UART0-RX umgelegt) einbinden, sonst bleiben IO35/UART0 unbelegt und die UI zeigt nur 2 Sensoren.
#ifndef ENABLE_LD2420
#define ENABLE_LD2420     0
#endif

// ---- Frontfilter: nur Personen VOR dem Geraet (Nutzer steht dahinter) ----
#ifndef MIN_FRONT_Y_MM
#define MIN_FRONT_Y_MM        150   // LD2450-Ziele mit y <= Wert (hinter/auf der Platine) werden verworfen
#endif
#ifndef MIN_RANGE_MM
#define MIN_RANGE_MM          800   // Blindzone: Ziele naeher als Wert werden verworfen. 800 statt 300: Rueckkeule (Nutzer hinter dem Geraet) erscheint nur mit y>0 im Nahbereich (LD2450-Y ist laut HLK immer positiv)
#endif
#ifndef SELF_ZONE_MM
#define SELF_ZONE_MM          1000  // Nahzone um das Geraet (= Nutzerkoerper): Spuren darunter werden nie gehalten/bestaetigt, LD2410C-Meldungen darunter werden ignoriert
#endif
#ifndef TRACK_LOST_FAR_MS
#define TRACK_LOST_FAR_MS     700   // Verlusttimeout fuer Ziele weiter als FAR_RANGE_MM (LD2450-Ausfaelle sind in der Ferne haeufiger)
#endif
#ifndef FAR_RANGE_MM
#define FAR_RANGE_MM          3000
#endif
#ifndef SHOW_RAW_DOTS
#define SHOW_RAW_DOTS         1     // 1 = rohe LD2450-Messpunkte (vor Spawn-Bestaetigung) als kleine weisse Ringe auf dem Display (wie im PC-Viewer)
#endif
#ifndef FRONT_HALF_FOV_DEG
#define FRONT_HALF_FOV_DEG    60.0f // Ziele mit |Azimut| groesser als Wert werden verworfen (<= RADAR_HALF_FOV_DEG)
#endif
#ifndef SHOW_RANGE_ONLY_RINGS
#define SHOW_RANGE_ONLY_RINGS 0     // 1 = Entfernungs-Boegen von LD2410C/LD2420 ohne LD2450-Spur anzeigen (zeigt auch den Nutzer hinter dem Geraet!)
#endif

// ---- Sensor-UARTs (nur RX-Leitung der ESP32-Seite; Sensor-TX -> ESP-RX; ESP-TX bleibt unbeschaltet) ----
#define PIN_RX_LD2450     22      // Serial2, CN1/P3 (IO22), 256000 Baud
#define PIN_RX_LD2410     27      // Serial1, CN1 (IO27),    256000 Baud
#if ENABLE_LD2420
#define PIN_RX_LD2420     35      // Serial (UART0, RX umgemappt), P3 (IO35, input-only), 115200 Baud
#endif
#define BAUD_LD2450       256000
#define BAUD_LD2410       256000
#define BAUD_LD2420       115200  // nur mit ENABLE_LD2420; LD2420 Firmware >= 1.5.3; aeltere FW: 256000 (siehe README)

// ---- PC-Anzeige ueber USB-Serial (UART0 / CH340): JSON-Zeilen fuer viewer/index.html (Web Serial) ----
// Nur ohne LD2420 (dann ist UART0 frei). Zeilen mit '#' sind Info. Befehle vom PC: "sim 1", "sim 0", "reset".
#ifndef PC_SERIAL_BAUD
#define PC_SERIAL_BAUD    460800
#endif
#ifndef PC_JSON_PERIOD_MS
#define PC_JSON_PERIOD_MS 50      // JSON-Periode: 20 Zeilen/s (zusaetzlich sofort nach jedem neuen LD2450-Frame)
#endif
#define PC_JSON_MIN_GAP_MS 25     // Mindestabstand zweier Zeilen (Bandbreite: Zeile <= ~1,1 KB -> < 46 KB/s bei 460800 Baud)
#define PC_SERIAL_JSON    (!ENABLE_LD2420)

// ---- Latenz / Praezision: jede LD2450-Messung wird sofort uebernommen, KEINE Glaettung/Interpolation/Vorhersage ----
// Rauschen wird durch Plausibilitaet unterdrueckt (Ausreisser-Pruefung), nicht durch Mitteln.
#ifndef POS_SMOOTH_ALPHA
#define POS_SMOOTH_ALPHA      1.0f  // 1.0 = Position = neueste Messung (ungeglaettet); <1 = EMA (z.B. 0.6) auf Kosten der Praezision
#endif
#ifndef TRACK_LOST_MS
#define TRACK_LOST_MS         300   // LD2450 meldet Ziel so lange nicht mehr -> sofort weg (nur ruhende, vom LD2410C bestaetigte Ziele werden gehalten)
#endif
#ifndef OUTLIER_JUMP_MM
#define OUTLIER_JUMP_MM       1500  // Sprung > Wert zwischen zwei Frames gilt als unplausibel: erst nach SPAWN_CONFIRM_FRAMES Frames in Folge akzeptiert
#endif
#ifndef SPAWN_CONFIRM_FRAMES
#define SPAWN_CONFIRM_FRAMES  2     // ein neues Ziel (oder ein Sprung) wird erst uebernommen, wenn es in 2 Frames in Folge am gleichen Ort steht (1 = sofort, ohne Pruefung)
#endif
#ifndef CAND_GATE_MM
#define CAND_GATE_MM          500   // zwei aufeinanderfolgende Messungen gelten als "gleiches neues Ziel", wenn naeher als Wert
#endif
#ifndef UI_RENDER_MS
#define UI_RENDER_MS          40    // Radar mindestens alle 40 ms neu zeichnen (25 FPS), zusaetzlich sofort nach jedem neuen Frame
#endif
#ifndef UI_MIN_FRAME_MS
#define UI_MIN_FRAME_MS       15    // Mindestabstand zwischen zwei Radar-Updates (Zeit fuer UART/Touch)
#endif
#ifndef UI_STATUS_MS
#define UI_STATUS_MS          500   // Statusleiste (Sensorpunkte) nur alle 500 ms bzw. bei Aenderung
#endif

// ---- Zeitlimits Sensorstatus ----
#define LINK_TIMEOUT_MS   1500    // keine gueltigen Daten laenger -> rot

// ---- Anzeige / Touch (unveraendert aus dem CSI-Projekt) ----
#define DISPLAY_ROTATION  1       // Querformat 320x240 (3 = um 180 Grad)
#define PIN_BOOT_BTN      0       // BOOT-Taste: Demo an/aus
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_X_MIN       200
#define TOUCH_X_MAX       3700
#define TOUCH_Y_MIN       240
#define TOUCH_Y_MAX       3800
#define TOUCH_DEBOUNCE_MS 300

// ---- Radar-Geometrie ----
#define RADAR_HALF_FOV_DEG 60.0f  // LD2450: +-60 Grad Azimut (nur Anzeige-Sektor; Filter: FRONT_HALF_FOV_DEG)
#define RADAR_ZOOM_A_M     3
#define RADAR_ZOOM_B_M     6
