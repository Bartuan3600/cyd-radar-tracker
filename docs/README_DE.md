# CYD-Tracker (2 Sensoren: LD2450 + LD2410C, nur Personen vor dem Gerät) – Kurzanleitung

Firmware: `firmware/` (PlatformIO-Env `cyd-tracker`; altes Projekt `firmware/` unverändert).
Image (aktuell, schnell/präzise): `CYD-Tracker-2sensor-fast.bin` (422 128 Byte, Merged-Image, Flash-Offset **0x0**). Die ältere `CYD-Tracker-2sensor.bin` bleibt unverändert. **Kein WLAN, kein Passwort.**
PC-Anzeige: `CYD-Radar-Viewer.zip` (Web-Serial-Viewer + dieselbe bin, siehe `README_Viewer.md` darin).

## Latenz & Präzision (neu in `CYD-Tracker-2sensor-fast.bin`)
Ziel: Anzeige **springt pro Messung auf den exakt gemessenen Punkt** (kein Gleiten), jede neue Messung sofort sichtbar, Rauschen nur durch Plausibilität unterdrückt.
- **Keine Glättung, keine Interpolation, keine Bewegungsvorhersage, keine Spur am Display.** Jeder LD2450-Frame (≈ 10 Hz) setzt die Position im selben Funktionsaufruf auf die gemessene Koordinate (`POS_SMOOTH_ALPHA` 1.0). Position am Gerät/Viewer in ganzen cm, Rohwerte in mm im Viewer.
- **Ausreißer durch Plausibilität**: ein Sprung > `OUTLIER_JUMP_MM` (1,5 m) zwischen zwei Frames – und jedes neue Ziel – wird erst übernommen, wenn es im nächsten Frame (≤ `CAND_MAX_AGE_MS`) am selben Ort (≤ `CAND_GATE_MM`) bestätigt wird. Einzelne Fehlmessungen erscheinen nie. Der Zähler verworfener Ausreißer steht im JSON (`ol`), im Viewer und auf der Detailseite.
- **Verlorenes Ziel sofort weg**: meldet der LD2450 ein bewegtes Ziel > `TRACK_LOST_MS` (300 ms) nicht mehr, verschwindet es. Gehalten wird nur ein **ruhendes** Ziel, das der LD2410C bestätigt (vorher 800 ms Totzeit, danach Halten auch für bewegte Ziele).
- **Verarbeitung/Ausgabe event-getrieben**: Fusion läuft in jedem `loop()`-Durchlauf; Display und JSON werden sofort nach jedem neuen LD2450-Frame ausgegeben (vorher: feste Takte 120 ms Display, 125 ms JSON).
- **Teilzeichnen**: nur der Radarbereich (240×218) wird in einem 8-Bit-Sprite gezeichnet und übertragen (statischer Hintergrund aus eigenem Sprite kopiert), nicht mehr der ganze Bildschirm (320×240 ≈ 60 ms SPI). Statusleiste nur alle `UI_STATUS_MS` bzw. bei Änderung, rechte Buttons nur bei Änderung. Ziel: ≥ 20 FPS; der **echte Wert wird am Gerät gemessen** und unten links im Radar angezeigt („Anz x fps y ms“, „LD2450 x/s lat z ms“), zusätzlich in der `#`-Zeile und im JSON-Feld `pf`.
- **JSON 20 Hz** (`PC_JSON_PERIOD_MS` 50) plus sofort nach jedem LD2450-Frame (min. Abstand `PC_JSON_MIN_GAP_MS` 25): typische Zeile 450–600 Byte, Worst case 1,15 KB → ca. 11 KB/s typisch, max. 46 KB/s Kapazität bei 460800 Baud; schreibt nie blockierend (volle Puffer → Zeile verworfen, Zähler `drop`).
- **Viewer**: zeichnet jede Zeile sofort per `requestAnimationFrame`, optionale **Spur** (Knopf „Spur“, Standard **AUS**) zeigt die letzten ~1 s echte LD2450-Messpunkte als einzelne Punkte (keine Linien); Tabellen mit Rohkoordinaten x/y in mm, Auflösung `res`, Position in ganzen cm; Anzeige von Zeilen/s, Bildern/s und Latenz.

### Neue Defines (Standard in `include/config.h`)
| Define | Standard | Wirkung / Kompromiss |
|---|---|---|
| `POS_SMOOTH_ALPHA` | 1.0 | 1.0 = Position = neueste Messung (exakt). < 1 = EMA (z. B. 0.6 ruhigeres, aber verzögertes Bild) |
| `TRACK_LOST_MS` | 300 | so lange ohne LD2450-Meldung → bewegtes Ziel weg (ruhende LD2410C-bestätigte werden gehalten) |
| `OUTLIER_JUMP_MM` | 1500 | Sprung zwischen zwei Frames darüber = unplausibel (zugleich Zuordnungs-Gate für gleiche ID) |
| `SPAWN_CONFIRM_FRAMES` | 2 | neues Ziel/Sprung erst nach N Frames in Folge am selben Ort; 1 = sofort, ohne Prüfung (Nachteil: Einzelausreißer sichtbar) |
| `CAND_GATE_MM` | 500 | max. Abstand zweier Bestätigungs-Messungen |
| `PC_JSON_PERIOD_MS` | 50 | JSON-Periode (20 Hz) |
| `PC_JSON_MIN_GAP_MS` | 25 | Mindestabstand der Zeilen (Bandbreitenschutz) |
| `UI_RENDER_MS` | 40 | Radar spätestens alle 40 ms neu zeichnen (25 FPS), sonst sofort nach jedem neuen Frame |
| `UI_MIN_FRAME_MS` | 15 | Mindestabstand zweier Radar-Updates (Zeit für UART/Touch) |
| `UI_STATUS_MS` | 500 | Statusleiste (Sensorpunkte) nur alle 500 ms oder bei Änderung |

Kompromisse: Ein **neues Ziel erscheint ≈ 100 ms später** (zweiter Frame zur Bestätigung). Bei Sprung > 1,5 m läuft die alte Spur noch bis `TRACK_LOST_MS`, bis die neue Position übernommen wurde; bei Zielen, die enger als 1,5 m zusammenstehen, sind ID-Vertauschungen möglich. Der LD2450 selbst liefert nur ≈ 10 Frames/s – schneller als das Gerät kann die Anzeige nicht aktualisieren, nur ohne zusätzliche Verzögerung.

## Was neu ist (gegenüber CYD-Tracker-fusion.bin)
- **Nur noch zwei Sensoren**: LD2450 (IO22) und LD2410C (IO27). Der **LD2420 ist optional** (`-DENABLE_LD2420=1`, Standard **AUS**): ohne ihn sind IO35/UART0 nicht für Sensoren belegt, die Statusleiste zeigt nur zwei Sensoren, Fusion/Bestätigung rechnen mit zwei Sensoren.
- **Frontfilter – nur Personen VOR dem Gerät.** **Nutzer hinter dem Gerät wird ausgefiltert.**
  - LD2450-Ziele mit `y <= MIN_FRONT_Y_MM` (150 mm) oder mit Winkel außerhalb ±`FRONT_HALF_FOV_DEG` (60°) werden verworfen (nie ein Track, nie auf dem Display, nicht im JSON-Track-Array).
  - Ziele näher als `MIN_RANGE_MM` (300 mm) werden verworfen.
  - Der **LD2410C kann nie allein einen Track oder Ring erzeugen**; er bestätigt/hält nur Tracks, die der LD2450 vorn gesehen hat. Ringe „nur Entfernung“ sind aus (`SHOW_RANGE_ONLY_RINGS` 0). **Ohne LD2450-Daten zeigt das Gerät nur den Sensorstatus, keine Personen.**
- **Bestätigt = LD2450 + LD2410C stimmen überein** (`FUSION_CONFIRM_MIN` = 2). Ein gehaltenes Ziel (LD2450 hat es verloren, nur LD2410C meldet Präsenz) bleibt sichtbar (Ring mit Punkt), ist aber **nie „bestätigt“**.
- **USB-Ausgabe für den PC** (UART0/CH340, **460800 Baud**): JSON-Zeilen für den Radar-Viewer, siehe unten.

## Defines (alle per `-D…` in `platformio.ini` `build_flags` oder `PLATFORMIO_BUILD_FLAGS` änderbar, Standard in `include/config.h`)
| Define | Standard | Bedeutung |
|---|---|---|
| `ENABLE_LD2420` | 0 | LD2420 als dritter Sensor (IO35, UART0-RX umgelegt). Dann **keine** USB-JSON-Ausgabe/-Befehle (UART0 ist belegt). |
| `MIN_FRONT_Y_MM` | 150 | LD2450-Ziele mit y ≤ Wert (hinter/neben dem Gerät) verwerfen |
| `FRONT_HALF_FOV_DEG` | 60.0 | Ziele mit Winkel > ±Wert zur Mittelachse verwerfen (kleiner = enger, z. B. 45) |
| `MIN_RANGE_MM` | 300 | Ziele näher als Wert verwerfen |
| `SHOW_RANGE_ONLY_RINGS` | 0 | 1 = gestrichelte „nur Entfernung“-Ringe des LD2410C ohne Track anzeigen |
| `PC_SERIAL_BAUD` | 460800 | Baudrate der USB-Ausgabe |
| (weitere) | | siehe Tabelle „Neue Defines“ oben |

Weitere Konstanten: `src/fusion.h` (`FUSION_*`: Gate = `OUTLIER_JUMP_MM`, Toleranz 900 mm, Haltezeit, …) und `include/config.h` (Pins, Baudraten, Zoomstufen, Link-Timeout 1,5 s, `RADAR_HALF_FOV_DEG` nur für die Anzeige).

## Status: bestätigt vs. ungetestet
**Bestätigt (am PC):**
- `pio run` baut ohne Fehler: Standard (Flash 27,2 %, RAM statisch 8,4 % + Sprites im Heap) und mit `-DENABLE_LD2420=1 -DSHOW_RANGE_ONLY_RINGS=1`.
- Host-Test `test/run_host_test.sh` (6 Konfigurationen: Standard, LD2420, Ringe, beides, `POS_SMOOTH_ALPHA=0.6f`, `SPAWN_CONFIRM_FRAMES=1`): 81 / 89 / 81 / 89 / 81 / 76 Prüfungen, 0 Fehler. Neu: Messung → Track im selben Aufruf (exakte Position, α = 1), Einzelsprung 1,9 m wird verworfen, Sprung über 2 Frames bestätigt, bewegtes Ziel nach ≈ 310 ms weg, ruhend + LD2410C gehalten / ohne LD2410C weg, Verarbeitungsrate ≈ 2,6 Mio. Frames/s am PC (0,39 µs/Frame; ESP32 geschätzt 10–30× langsamer, **nicht gemessen**). Frontfilter-Tests unverändert bestanden.
- JSON-Test `test/run_json_test.sh`: 181 Prüfungen + 80 Schnappschüsse mit echtem `json`-Parser (1370 Prüfungen); Sendetakt simuliert: 20,0 Zeilen/s, größter Abstand 50 ms, längste Zeile 602 Byte (≈ 11 KB/s), Worst-Case-Zeile 1149 Byte (≈ 23 KB/s < 46 KB/s, Puffer 3072, TX-Puffer 2048).
- Viewer-Test `test/viewer_mock_test.py` (Mock-Web-Serial + headless Chrome): ALLES OK, u. a. Echtzeitlauf mit 20 Zeilen/s: 19,8 Zeilen/s verarbeitet, jede Zeile gezeichnet (`requestAnimationFrame`), Latenz Zeile → Zeichnung Ø 6,9 ms, max 16,4 ms; Spur Standard AUS, Ganz-cm-Anzeige, `res`/mm-Tabellen.
- UI-Render (`tools/render_ui.py`, Bilder in `tracker-ui-render/`): zeigt Layout/Farben der echten Zeichenlogik, **nicht** pixelgenau (andere Schrift); der Teilzeichen-Pfad (Region/Status/Buttons) läuft im Host-Mock durch.
- Image: Bootloader@0x1000, Partitionen@0x8000, boot_app0@0xE000, App@0x10000; die ersten 64 KiB sind identisch mit `CYD-Tracker-2sensor.bin`.

**UNGETESTET (keine Hardware):** tatsächliche Display-FPS und Zeichenzeit (SPI 40 MHz, Teilzeichnen), Speicher für die drei 8-Bit-Sprites (≈ 112 KB Heap; bei Fehlschlag Rückfall ohne Sprites), echte LD2450-Rauschwerte (Eignung von 1,5 m / 2 Frames), Display/Touch, beide Sensoren am echten UART, Verdrahtung, Reichweiten, Stromversorgung, X-Vorzeichen und Verhalten des LD2450 für Ziele hinter der Platine (Frontfilter wirkt nach der gemeldeten y-Koordinate), USB-JSON auf echtem CH340, `server.ps1`/`start-viewer.bat` unter Windows.

## Verdrahtung (nur RX; ESP-TX zu den Sensoren nicht nötig)
| Sensor | Sensor-Pin | CYD-Pin | UART / Baud | Versorgung |
|---|---|---|---|---|
| LD2450 | TX → | IO22 (CN1/P3) | Serial2, 256000 | 5 V (~120 mA) |
| LD2410C | TX → | IO27 (CN1) | Serial1, 256000 | 5 V (5–12 V, ~79 mA) |
| LD2420 (optional, `ENABLE_LD2420=1`) | **OT2 (J2-Pin 5) = UART-TX** → | IO35 (P3, nur Eingang) | UART0-RX umgelegt, 115200 | 3,3 V (~50 mA) |
| alle | GND | GND (gemeinsam!) | – | – |

- LD2420: **OT2 (J2-Pin 5) ist UART-TX, OT1 ist der Präsenz-Ausgang** (digital, wird von dieser Firmware nicht gelesen). Firmware ≥ 1.5.3: 115200 Baud, ältere 256000 → `BAUD_LD2420` ändern.
- Logikpegel aller Sensoren 3,3 V → direkt an die ESP32-Eingänge.
- Pins: IO22, IO27, IO35 sind auf dem CYD herausgeführt und nicht von TFT (12/13/14/15/2/21), Touch (25/32/33/36/39) noch SD/Lautsprecher belegt.
- 5 V von VIN/USB-5V des CYD; Gesamt ca. 250 mA für Sensoren + CYD → Netzteil/Powerbank ≥ 1 A. **Spannung am Pin vor dem Anstecken messen.**
- Standard (ohne LD2420): UART0/USB bleibt frei für die PC-Anzeige. Mit `ENABLE_LD2420=1` ist UART0-RX auf IO35 umgelegt und die USB-Ausgabe aus.

## Flashen (hier wurde nichts geflasht)
```
esptool.py --chip esp32 --port <PORT> --baud 460800 write_flash 0x0 CYD-Tracker-2sensor-fast.bin
```
Alternativ im Browser: ESP Web Flasher https://web.esphome.io/ (Chrome/Edge, „Install“ → Datei wählen; bei Verbindungsproblemen BOOT-Taste halten, während „Connect“ gedrückt wird). Windows braucht ggf. den CH340-Treiber. Port: `COMx` / `/dev/ttyUSB0`.
Selbst bauen: `cd csi-cyd/firmware-tracker && pio run`. Host-Tests: `test/run_host_test.sh`, `test/run_json_test.sh` (g++ oder `pip install ziglang`).

## Bedienung
- Oben Statusleiste: je Sensor (LD2450, LD2410C) grüner Punkt = Daten, rot = keine Daten. „SIM“ = Demo aktiv.
- Touch-Buttons rechts: **ZOOM** (3 m / 6 m), **SEITE** (Radar → Sensor-Details → Einstellungen), **DEMO** (Simulation an/aus; BOOT-Taste macht dasselbe).
- Radar (Teilzeichnen, unten links Messwerte „Anz … fps“, „LD2450 …/s lat …“): Sektor ±60° nach vorn, bis 6 m, Legende „nur Ziele vor dem Geraet“. Symbole: gefüllt = bewegt, Ring = ruhend, Ring mit Punkt = gehalten (nur LD2410C), orange = unbestätigt, grün = bestätigt (LD2450 + LD2410C).
- Sensor-Seite: Rohwerte je Sensor, Zähler, „Frontfilter: N verworfen“. Einstellungen (im Flash gespeichert): Toleranz, Haltezeit, X spiegeln, Standardwerte.
- Fehlende Sensoren blockieren nichts. Ohne LD2450 erscheinen keine Personen (nur Statusleiste).

## PC-Anzeige über USB
- Firmware sendet auf UART0 (USB/CH340, **460800 Baud, 8N1**) 20× pro Sekunde (50 ms, zusätzlich sofort nach jedem LD2450-Frame) eine JSON-Zeile (Format wie `firmware-stm32/src/state_json.h`, nur 2 Sensoren; Zusatzfelder `ns`, `fd`, `fy`, `fm`, `fa`, `r450[].f`, neu `ol` Ausreißer, `pf` [fps×10, Render-ms, LD2450-Hz×10, Latenz-ms], `cf` [α×100, Lost-ms, Sprung-mm, Bestätigungsframes, Sendetakt-ms]). Alle 5 s eine Infozeile mit `#`. Es gibt keinen Debug-Text auf UART0.
- Blockiert nie: die Zeile wird nur geschrieben, wenn der Sendepuffer sie ganz aufnimmt, sonst verworfen (Zähler `drop` in der Infozeile).
- Befehle vom PC (Zeilen mit `\n`): `sim 1`, `sim 0` (Demo), `reset` (Ziele löschen).
- Beim Einschalten schreibt der ESP32-Bootloader kurz Text mit 115200 Baud an den Port; der Viewer ignoriert Zeilen, die nicht mit `{` oder `#` beginnen.
- Viewer: `csi-cyd/firmware-tracker/viewer/` bzw. `CYD-Radar-Viewer.zip` → `start-viewer.bat` doppelklicken (Chrome/Edge), Port wählen, „Verbinden“.

## Fusionsregeln
1. Nur der LD2450 erzeugt Tracks, und nur aus Zielen, die den Frontfilter bestehen. Zuordnung nächster Nachbar (Gate `OUTLIER_JUMP_MM` = 1500 mm); Position = exakt die neueste Messung (`POS_SMOOTH_ALPHA` 1.0).
2. Neues Ziel oder Sprung > 1500 mm: erst nach Bestätigung im nächsten Frame (`SPAWN_CONFIRM_FRAMES` 2, `CAND_GATE_MM` 500) – sonst als Ausreißer verworfen (Zähler `ol`).
3. Bewegt/ruhend nach Geschwindigkeit (ruhend nach 1,5 s ohne Bewegung).
4. Sieht der LD2450 ein Ziel > 300 ms (`TRACK_LOST_MS`) nicht mehr, ist es weg. Ausnahme: ein **ruhendes** Ziel mit passender LD2410C-Präsenz (≈ √(x²+y²) ± 900 mm) wird nur **gehalten**; letzte Übereinstimmung älter als 3 s → weg.
5. Bestätigt = LD2450 und LD2410C stimmen überein (Fenster 1,5 s); gehaltene Ziele sind nie bestätigt.
6. Taucht der LD2450 wieder auf, behält das Ziel seine ID.

## Bekannte Grenzen
- **Der LD2410C hat keine Richtung.** Steht der Nutzer hinter dem Gerät in ähnlicher Entfernung (± 900 mm) wie ein Ziel vorn, kann der LD2410C dieses vordere Ziel „bestätigen“ oder halten. Der Nutzer selbst wird nie als Person angezeigt (Frontfilter am LD2450, LD2410C erzeugt keinen Track), aber er kann die Bestätigung eines echten Ziels vortäuschen.
- Meldet der LD2450 Ziele hinter der Platine mit falschem Vorzeichen/Spiegelung, greift der Filter nicht – bitte am Gerät prüfen (Sensor-Seite zeigt Rohwerte, „Frontfilter: N verworfen“).
- X-Richtung des LD2450 nicht verifiziert → ggf. „X spiegeln“.
- Der Viewer/das Gerät zeigen Messpunkte, die springen – gewollt. Der LD2450 selbst rauscht (±10–30 cm bei ruhenden Personen, Mehrwege-Reflexe): Ausreißer-Prüfung fängt nur große Sprünge, kleines Rauschen bleibt sichtbar (wer es ruhiger will: `POS_SMOOTH_ALPHA=0.6`).
- Toleranz 900 mm, weil die Messzellen der Radare 0,7–0,75 m breit sind.
- LD2450: stehende Ziele evtl. mit Geschwindigkeit 0 und nach kurzer Zeit ganz weg (genau dafür das „Halten“).
- Kein Akku-Management, keine Funkverbindung. Radar-Zulassung (RED) ist nicht Teil dieser Firmware.
