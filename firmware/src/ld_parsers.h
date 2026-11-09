// Schlanke Parser fuer LD2450 / LD2410C / LD2420 (kein Arduino-Code -> auf dem PC testbar).
// Alle Parser: Byte fuer Byte fuettern (feed), robust gegen Muell und Sync-Verlust:
// bei falschem Header/Footer wird ein Byte verworfen und neu gesucht.
//
// STATUS: Gegen synthetische Frames am PC getestet. KEIN Sensor war angeschlossen
// -> Verhalten an echter Hardware UNGETESTET. Quellen siehe README_Tracker.md.
#pragma once
#include <stdint.h>
#include <string.h>

// ======================= LD2450 =======================
// Frame (30 Byte): AA FF 03 00 | 3 x Ziel (8 Byte) | 55 CC
// Ziel: X(int16) Y(int16) Speed(int16) Resolution(uint16), little endian.
// Vorzeichen-Format: Bit15 = 1 -> positiv, Bit15 = 0 -> negativ, Betrag = Bits 0..14.
// X/Y in mm, Speed in cm/s (Roh). Ziel leer, wenn X=Y=0.   (Quelle: ESPHome ld2450.cpp)
struct Target2450 { bool valid; int16_t x, y; int16_t speed; uint16_t res; };
struct Frame2450  { Target2450 t[3]; };

class Ld2450Parser {
 public:
  static const int FRAME = 30;
  uint32_t frames = 0, resyncs = 0, bytes = 0;
  Frame2450 last = {};
  // true, wenn mit diesem Byte ein gueltiger Frame fertig wurde (-> last)
  bool feed(uint8_t b) {
    bytes++;
    if (len_ >= FRAME) shift_(1);
    buf_[len_++] = b;
    bool got = false;
    for (;;) {
      while (len_ > 0 && !headerPrefix_()) { shift_(1); resyncs++; }
      if (len_ < FRAME) break;
      if (buf_[28] == 0x55 && buf_[29] == 0xCC) {
        decode_();
        shift_(FRAME);
        frames++;
        got = true;
        break;
      }
      shift_(1); resyncs++;       // Footer falsch -> ein Byte weiter suchen
    }
    return got;
  }
  static int16_t sm(uint16_t raw) {  // Sign-Magnitude (Bit15 gesetzt = positiv)
    int16_t v = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? v : (int16_t)-v;
  }
 private:
  uint8_t buf_[FRAME]; int len_ = 0;
  bool headerPrefix_() const {
    static const uint8_t H[4] = {0xAA, 0xFF, 0x03, 0x00};
    int n = len_ < 4 ? len_ : 4;
    for (int i = 0; i < n; i++) if (buf_[i] != H[i]) return false;
    return true;
  }
  void shift_(int n) {
    if (n >= len_) { len_ = 0; return; }
    memmove(buf_, buf_ + n, len_ - n); len_ -= n;
  }
  void decode_() {
    for (int i = 0; i < 3; i++) {
      const uint8_t* p = buf_ + 4 + i * 8;
      Target2450& t = last.t[i];
      t.x = sm(p[0] | (p[1] << 8));
      t.y = sm(p[2] | (p[3] << 8));
      t.speed = sm(p[4] | (p[5] << 8));
      t.res = p[6] | (p[7] << 8);
      t.valid = !(t.x == 0 && t.y == 0);
    }
  }
};

// ======================= LD2410C =======================
// Datenframe: F4 F3 F2 F1 | len(2, LE) | Typ(1: 02 normal, 01 engineering) AA | Zustand |
//   Bewegungs-Dist(2, cm) | Bew.-Energie | Stand-Dist(2, cm) | Stand-Energie | Erkennungs-Dist(2, cm)
//   [engineering: weitere Bytes] | 55 00 | F8 F7 F6 F5
// Zustand: 0 kein Ziel, 1 bewegt, 2 stehend, 3 beides.   (Quelle: ESPHome ld2410.cpp)
// Normalmodus: len = 13 (0x0D) -> 23 Byte Gesamtlaenge.
struct Report2410 {
  uint8_t state;          // Bit0 = bewegt, Bit1 = stehend
  uint16_t moveDist, stillDist, detDist;   // cm
  uint8_t moveEnergy, stillEnergy;
  bool engineering;
};

class Ld2410Parser {
 public:
  static const int MAXLEN = 64;            // maximale Payload-Laenge, was groesser ist = Muell
  uint32_t frames = 0, resyncs = 0, bytes = 0;
  Report2410 last = {};
  bool feed(uint8_t b) {
    bytes++;
    if (len_ >= BUF) shift_(1);
    buf_[len_++] = b;
    bool got = false;
    for (;;) {
      while (len_ > 0 && !headerPrefix_()) { shift_(1); resyncs++; }
      if (len_ < 6) break;
      int pl = buf_[4] | (buf_[5] << 8);
      if (pl < 11 || pl > MAXLEN) { shift_(1); resyncs++; continue; }
      int total = 4 + 2 + pl + 4;
      if (len_ < total) break;
      const uint8_t* p = buf_ + 6;
      static const uint8_t F[4] = {0xF8, 0xF7, 0xF6, 0xF5};
      bool ok = memcmp(buf_ + 6 + pl, F, 4) == 0 && p[1] == 0xAA &&
                (p[0] == 0x01 || p[0] == 0x02) && p[pl - 2] == 0x55 && p[pl - 1] == 0x00;
      if (!ok) { shift_(1); resyncs++; continue; }
      last.engineering = (p[0] == 0x01);
      last.state = p[2] & 0x03;
      last.moveDist = p[3] | (p[4] << 8);
      last.moveEnergy = p[5];
      last.stillDist = p[6] | (p[7] << 8);
      last.stillEnergy = p[8];
      last.detDist = p[9] | (p[10] << 8);
      shift_(total); frames++; got = true;
      break;
    }
    return got;
  }
 private:
  static const int BUF = 4 + 2 + MAXLEN + 4;
  uint8_t buf_[BUF]; int len_ = 0;
  bool headerPrefix_() const {
    static const uint8_t H[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    int n = len_ < 4 ? len_ : 4;
    for (int i = 0; i < n; i++) if (buf_[i] != H[i]) return false;
    return true;
  }
  void shift_(int n) {
    if (n >= len_) { len_ = 0; return; }
    memmove(buf_, buf_ + n, len_ - n); len_ -= n;
  }
};

// ======================= LD2420 =======================
// Standard-Ausgabe (Simple Mode, Firmware >= 1.5.3: 115200 Baud). Pins J2: OT2 (J2-Pin 5) ist UART-TX,
// OT1 ist der Praesenz-Ausgang (digital), RX = Pin 4. Fuer diese Firmware wird nur OT2 (UART-TX) angeschlossen.
//   ASCII-Zeilen "ON" / "OFF" und "Range <cm>", CRLF-terminiert.  (Quelle: HLK-Handbuch, ESPHome ld2420.cpp)
// Optional Energy-Frame (nur nach Config-Befehl, Firmware >= 1.5.4, hier NICHT aktiviert, daher
// nur mitgeparst, falls er je kommt): F4 F3 F2 F1 | 23 00 | Presence(1) Range(2, LE) | 16 x uint16 | F8 F7 F6 F5
// -> Energy-Frame-Parser ist ungetestet UND wird von der Firmware nie angefordert.
struct Report2420 {
  bool presence;
  bool rangeValid;
  uint16_t range;          // cm (letzter "Range"-Wert)
  bool fromEnergyFrame;
};

class Ld2420Parser {
 public:
  uint32_t lines = 0, frames = 0, resyncs = 0, bytes = 0;
  Report2420 last = {};
  // true, wenn sich mit diesem Byte ein Zustand (ON/OFF/Range/Energy-Frame) aktualisiert hat
  bool feed(uint8_t b) {
    bytes++;
    win_ = (win_ << 8) | b;
    if (bin_) return feedBin_(b);
    if (win_ == 0xF4F3F2F1u) {     // Energy-Frame-Header erkannt
      bin_ = true; blen_ = 4;
      bbuf_[0] = 0xF4; bbuf_[1] = 0xF3; bbuf_[2] = 0xF2; bbuf_[3] = 0xF1;
      llen_ = 0;
      return false;
    }
    if (b == '\n' || b == '\r') {
      bool got = false;
      if (llen_ > 0) { line_[llen_] = 0; got = parseLine_(); llen_ = 0; }
      return got;
    }
    if (b >= 0x20 && b < 0x7F) {
      if (llen_ < LINE - 1) line_[llen_++] = (char)b;
      else { llen_ = 0; resyncs++; }
    }                               // andere Bytes (z.B. Boot-Muell) werden ignoriert
    return false;
  }
 private:
  static const int LINE = 24, BINLEN = 4 + 2 + 35 + 4;
  char line_[LINE]; int llen_ = 0;
  uint8_t bbuf_[BINLEN]; int blen_ = 0; bool bin_ = false;
  uint32_t win_ = 0;
  bool parseLine_() {
    if (llen_ >= 2 && line_[0] == 'O' && line_[1] == 'N' && llen_ == 2) { last.presence = true; last.fromEnergyFrame = false; lines++; return true; }
    if (llen_ == 3 && line_[0] == 'O' && line_[1] == 'F' && line_[2] == 'F') { last.presence = false; last.fromEnergyFrame = false; lines++; return true; }
    if (llen_ > 6 && memcmp(line_, "Range ", 6) == 0) {
      uint32_t v = 0; int i = 6, nd = 0;
      for (; i < llen_ && line_[i] >= '0' && line_[i] <= '9' && nd < 5; i++, nd++) v = v * 10 + (line_[i] - '0');
      if (nd == 0 || v > 2000) { resyncs++; return false; }   // 20 m = sicher Muell
      last.range = (uint16_t)v; last.rangeValid = true; last.fromEnergyFrame = false; lines++;
      return true;
    }
    resyncs++;
    return false;
  }
  bool feedBin_(uint8_t b) {
    bbuf_[blen_++] = b;
    if (blen_ < 6) return false;
    int pl = bbuf_[4] | (bbuf_[5] << 8);
    if (pl != 35) { bin_ = false; resyncs++; return false; }   // nur die bekannte Laenge
    if (blen_ < BINLEN) return false;
    static const uint8_t F[4] = {0xF8, 0xF7, 0xF6, 0xF5};
    bin_ = false;
    if (memcmp(bbuf_ + 6 + pl, F, 4) != 0) { resyncs++; return false; }
    last.presence = bbuf_[6] != 0;
    last.range = bbuf_[7] | (bbuf_[8] << 8);
    last.rangeValid = true; last.fromEnergyFrame = true;
    frames++;
    return true;
  }
};
