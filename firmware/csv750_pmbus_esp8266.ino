/*
 * CSV750 / Lenovo 700-013700 PSU - PMBus monitor for ESP8266 (NodeMCU / Wemos D1 mini)
 *
 * Wiring (PSU card edge -> ESP8266):
 *   S11 SDA     -> D2 (GPIO4)
 *   S12 SCL     -> D1 (GPIO5)
 *   S10 RETURN  -> GND
 *   Pull-ups: 4.7k from SDA and SCL to 3.3V ONLY if the PSU lines measure ~0V idle.
 *   If SDA/SCL measure above 3.6V idle, use a BSS138 level shifter instead.
 *
 * Read-only: this sketch never writes configuration to the PSU.
 */

#include <Wire.h>

const uint8_t SDA_PIN = 4;   // D2
const uint8_t SCL_PIN = 5;   // D1
const uint32_t POLL_MS = 2000;
const uint16_t GAP_MS = 20;               // CSV2000BP note: >= 15 ms between I2C transactions
const uint32_t STRETCH_LIMIT_US = 100000; // CSV2000BP note: PSU may stretch SCL up to 100 ms

uint8_t psuAddr = 0;         // found by scan (expected 0x68 with S15 open)

// ---- PMBus command codes ----
const uint8_t CMD_VOUT_MODE    = 0x20;
const uint8_t CMD_STATUS_WORD  = 0x79;
const uint8_t CMD_READ_VIN     = 0x88;
const uint8_t CMD_READ_IIN     = 0x89;
const uint8_t CMD_READ_VOUT    = 0x8B;
const uint8_t CMD_READ_IOUT    = 0x8C;
const uint8_t CMD_READ_TEMP1   = 0x8D;
const uint8_t CMD_READ_TEMP2   = 0x8E;
const uint8_t CMD_READ_FAN1    = 0x90;
const uint8_t CMD_READ_POUT    = 0x96;
const uint8_t CMD_READ_PIN     = 0x97;
const uint8_t CMD_MFR_ID       = 0x99;
const uint8_t CMD_MFR_MODEL    = 0x9A;
const uint8_t CMD_MFR_REVISION = 0x9B;

int8_t voutExp = 0;          // LINEAR16 exponent from VOUT_MODE

// ---------- low-level I2C helpers ----------
bool readByte(uint8_t cmd, uint8_t &out) {
  delay(GAP_MS);
  Wire.beginTransmission(psuAddr);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) return false;   // repeated start
  if (Wire.requestFrom(psuAddr, (size_t)1) != 1) return false;
  out = Wire.read();
  return true;
}

bool readWord(uint8_t cmd, uint16_t &out) {
  delay(GAP_MS);
  Wire.beginTransmission(psuAddr);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(psuAddr, (size_t)2) != 2) return false;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  out = ((uint16_t)hi << 8) | lo;
  return true;
}

// PMBus block read: first byte is the length
bool readBlock(uint8_t cmd, char *buf, size_t bufLen) {
  delay(GAP_MS);
  Wire.beginTransmission(psuAddr);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom(psuAddr, (size_t)(bufLen));
  if (got < 1) return false;
  uint8_t len = Wire.read();
  size_t i = 0;
  while (Wire.available() && i < len && i < bufLen - 1) {
    char c = (char)Wire.read();
    buf[i++] = (c >= 32 && c < 127) ? c : '.';
  }
  while (Wire.available()) Wire.read();
  buf[i] = '\0';
  return len > 0;
}

// ---------- PMBus number formats ----------
float linear11(uint16_t raw) {
  int8_t e = (raw >> 11) & 0x1F;
  if (e > 15) e -= 32;
  int16_t m = raw & 0x7FF;
  if (m > 1023) m -= 2048;
  return m * powf(2.0f, e);
}

float linear16(uint16_t raw) {
  return raw * powf(2.0f, voutExp);
}

// ---------- diagnostics for readings that look wrong ----------
// PMBus QUERY (0x1A): block write 1 byte (the command), block read 1 byte of support info.
// bit7 = supported, bit6 = write, bit5 = read, bits4:2 = data format code.
bool queryInfo(uint8_t cmd, uint8_t &info) {
  delay(GAP_MS);
  Wire.beginTransmission(psuAddr);
  Wire.write(0x1A);
  Wire.write(1);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(psuAddr, (size_t)2) != 2) return false;
  uint8_t len = Wire.read();
  info = Wire.read();
  return len == 1;
}

void printDiag(const char *label, uint8_t cmd) {
  uint16_t raw;
  if (!readWord(cmd, raw)) { Serial.printf("DIAG %-5s 0x%02X (n/a)\n", label, cmd); return; }
  Serial.printf("DIAG %-5s 0x%02X raw=0x%04X lo=0x%02X hi=0x%02X  linear11=%.3f  int16=%d  uint16=%u  linear16=%.4f\n",
                label, cmd, raw, raw & 0xFF, raw >> 8, linear11(raw), (int16_t)raw, raw, linear16(raw));
}

void printQueries() {
  const struct { const char *name; uint8_t cmd; } list[] = {
    {"VIN", CMD_READ_VIN}, {"IIN", CMD_READ_IIN}, {"VOUT", CMD_READ_VOUT},
    {"IOUT", CMD_READ_IOUT}, {"POUT", CMD_READ_POUT}, {"PIN", CMD_READ_PIN},
    {"TEMP1", CMD_READ_TEMP1}, {"FAN", CMD_READ_FAN1}
  };
  for (const auto &e : list) {
    uint8_t info;
    if (queryInfo(e.cmd, info))
      Serial.printf("QUERY %-5s 0x%02X info=0x%02X supported=%d write=%d read=%d format_code=%d\n",
                    e.name, e.cmd, info, (info >> 7) & 1, (info >> 6) & 1, (info >> 5) & 1, (info >> 2) & 7);
    else
      Serial.printf("QUERY %-5s 0x%02X (failed / not supported)\n", e.name, e.cmd);
  }
}

// ---------- bus scan ----------
void scanBus() {
  Serial.println(F("\nScanning I2C bus..."));
  uint8_t fallback = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X\n", a);
      if (a >= 0x58 && a <= 0x6F && !psuAddr) psuAddr = a;      // typical PSU range
      else if ((a < 0x50 || a > 0x57) && !fallback) fallback = a; // skip FRU EEPROM
    }
  }
  if (!psuAddr) psuAddr = fallback;
  if (psuAddr) Serial.printf("Using PSU address 0x%02X\n", psuAddr);
  else Serial.println(F("No PSU found - check wiring, ground, pull-ups."));
}

void printIdentity() {
  char buf[33];
  if (readBlock(CMD_MFR_ID, buf, sizeof(buf)))       Serial.printf("MFR_ID       : %s\n", buf);
  if (readBlock(CMD_MFR_MODEL, buf, sizeof(buf)))    Serial.printf("MFR_MODEL    : %s\n", buf);
  if (readBlock(CMD_MFR_REVISION, buf, sizeof(buf))) Serial.printf("MFR_REVISION : %s\n", buf);

  uint8_t mode;
  if (readByte(CMD_VOUT_MODE, mode)) {
    int8_t e = mode & 0x1F;
    if (e > 15) e -= 32;
    voutExp = e;
    Serial.printf("VOUT_MODE    : 0x%02X (exponent %d)\n", mode, voutExp);
  } else {
    voutExp = -9;  // common default; adjust if VOUT reads wrong
    Serial.println(F("VOUT_MODE read failed, assuming exponent -9"));
  }
}

void printReading(const char *label, uint8_t cmd, bool isVout, const char *unit) {
  uint16_t raw;
  if (readWord(cmd, raw)) {
    float v = isVout ? linear16(raw) : linear11(raw);
    Serial.printf("%-6s %8.2f %-3s raw=0x%04X\n", label, v, unit, raw);
  } else {
    Serial.printf("%-6s   (n/a)\n", label);
  }
}

void printStatus() {
  uint16_t s;
  if (!readWord(CMD_STATUS_WORD, s)) { Serial.println(F("STATUS (n/a)")); return; }
  Serial.printf("STATUS 0x%04X", s);
  if (s == 0) { Serial.println(F("  OK")); return; }
  if (s & (1 << 6))  Serial.print(F(" OFF"));
  if (s & (1 << 5))  Serial.print(F(" VOUT_OV"));
  if (s & (1 << 4))  Serial.print(F(" IOUT_OC"));
  if (s & (1 << 3))  Serial.print(F(" VIN_UV"));
  if (s & (1 << 2))  Serial.print(F(" TEMP"));
  if (s & (1 << 1))  Serial.print(F(" CML"));
  if (s & (1 << 15)) Serial.print(F(" VOUT"));
  if (s & (1 << 14)) Serial.print(F(" IOUT/POUT"));
  if (s & (1 << 13)) Serial.print(F(" INPUT"));
  if (s & (1 << 11)) Serial.print(F(" POWER_GOOD#"));
  if (s & (1 << 10)) Serial.print(F(" FANS"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);              // PMBus standard speed
  Wire.setClockStretchLimit(STRETCH_LIMIT_US);  // PSU stretches SCL after each byte (us)

  scanBus();
  if (psuAddr) { printIdentity(); printQueries(); }
}

void loop() {
  if (!psuAddr) { delay(5000); scanBus(); if (psuAddr) { printIdentity(); printQueries(); } return; }

  Serial.println(F("\n---- PSU ----"));
  printReading("VIN",  CMD_READ_VIN,   false, "V");
  printReading("IIN",  CMD_READ_IIN,   false, "A");
  printReading("PIN",  CMD_READ_PIN,   false, "W");
  printReading("VOUT", CMD_READ_VOUT,  true,  "V");
  printReading("IOUT", CMD_READ_IOUT,  false, "A");
  printReading("POUT", CMD_READ_POUT,  false, "W");
  printReading("TEMP1",CMD_READ_TEMP1, false, "C");
  printReading("TEMP2",CMD_READ_TEMP2, false, "C");
  printReading("FAN",  CMD_READ_FAN1,  false, "RPM");
  printStatus();
  printDiag("IIN",  CMD_READ_IIN);
  printDiag("IOUT", CMD_READ_IOUT);
  printDiag("POUT", CMD_READ_POUT);
  printDiag("PIN",  CMD_READ_PIN);

  delay(POLL_MS);
}
