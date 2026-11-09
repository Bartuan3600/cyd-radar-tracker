# CYD-Radar-Viewer – Radar-Anzeige am PC

Zeigt auf dem PC, was das CYD (Firmware `CYD-Tracker-2sensor-fast.bin`) sieht: nur Personen **vor** dem Gerät, zwei Sensoren (LD2450 + LD2410C). Nichts hinter dem Gerät wird gezeichnet.

## Start (Windows)
1. CYD per USB-Kabel an den PC (ggf. CH340-Treiber installieren). Firmware `CYD-Tracker-2sensor-fast.bin` muss geflasht sein (Flash-Offset 0x0).
2. In den Ordner `viewer` gehen und **`start-viewer.bat` doppelklicken**. Das schwarze Fenster offen lassen – es öffnet `http://localhost:8000`.
3. **Chrome oder Edge** verwenden (Web Serial; Firefox/Safari gehen nicht).
4. Auf der Seite **„Verbinden“** klicken und den Port des CYD wählen („USB-SERIAL CH340 (COMx)“). Baudrate **460800** (voreingestellt, wie in der Firmware).
5. Es erscheinen Radar, Sensorstatus (grün/rot), Gegnerliste. Knöpfe: **Zoom** (3/6 m), **X spiegeln**, **Simulation** (Demo auf dem CYD an/aus), **Reset** (Ziele löschen), **Spur** (Standard aus).

## Hinweise
- Port belegt? Arduino-Monitor/PuTTY/anderes Browserfenster schließen.
- „Nur Datenmüll“ → Baudrate prüfen (Firmware: 460800; bei Änderung in der Firmware auch im Viewer wählen, dann Trennen/Verbinden).
- Sensoren rot: Verdrahtung prüfen (LD2450 TX→IO22, LD2410C TX→IO27, gemeinsame Masse).
- Ohne LD2450 zeigt der Viewer keine Personen (der LD2410C erzeugt nie allein ein Ziel).
- Server ohne Windows: `python3 -m http.server 8000` im Ordner `viewer`, dann `http://localhost:8000` in Chrome/Edge.
- Ungetestet an echter Hardware; `server.ps1`/`start-viewer.bat` wurden nicht unter Windows ausgeführt. Getestet: Viewer gegen Mock-Web-Serial in headless Chrome (siehe `tests/`).

## Schnelle, präzise Anzeige (neu)
- Jede JSON-Zeile (20/s) wird sofort beim nächsten Bildaufbau gezeichnet (`requestAnimationFrame`, keine zusätzliche Verzögerung; 60-ms-Sicherung für verdeckte Tabs). Die Punkte **springen pro Messung** – keine Glättung, keine Interpolation, keine Vorhersage. Der LD2450 selbst liefert ≈ 10 Messungen/s.
- **Spur** (Knopf, Standard AUS): die letzten ~1 s echte LD2450-Messpunkte als einzelne, verblassende Punkte (keine Linien). Eine Messung = ein Punkt.
- Position an jedem Ziel und in der Fusionstabelle in **ganzen cm**; die LD2450-Tabelle zeigt Rohkoordinaten **X/Y in mm** und die Auflösung **Res (mm)**.
- Statuszeile: Zeilen/s, Bilder/s und Latenz Zeile → Zeichnung. Zeile unter den Knöpfen: Firmware-Konfiguration (α, Lost-Zeit, Sprungschwelle, Sendetakt), verworfene Ausreißer und die am Gerät gemessene Display-FPS/Latenz.
- Messbar: `tests/viewer_mock_test.py` (Zeilen/s, Zeichnungen, Latenz im Echtzeitlauf; Log `tests/viewer_test_log_fast.txt`, Bilder `tests/CYD-viewer-mock-fast*.png`). Ungetestet an echter Hardware.
