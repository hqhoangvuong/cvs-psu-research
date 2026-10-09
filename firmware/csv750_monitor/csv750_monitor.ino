// CSV750BP (part 00YL557) PMBus monitor for ESP8266 (Arduino core). Read-only.
//
// Built from bench measurements, see CSV750BP_PMBus_interface.md. Only commands that answered with a
// valid PEC are polled. The 750BP does NOT serve READ_VOUT/IOUT/IIN/POUT, STATUS_WORD or CAPABILITY,
// so nothing here reports output current or voltage.
//
// Wiring (all grounds common; ESP8266 is 3.3 V logic):
//   ESP8266 D2 (GPIO4) -> PSU S11 SDA
//   ESP8266 D1 (GPIO5) -> PSU S12 SCL
//   ESP8266 GND        -> PSU S10 RETURN
//   2.2 kOhm pull-ups from SDA and SCL to 3.3 V. 10 kOhm made the PSU stop answering.
//   S15 ADDRESS open = 7-bit 0x68. PSON (S16/S17) is not driven by this sketch.
//
// Serial 115200 baud. Send 'c' to toggle CSV output, 'r' to re-read the identity, 'x' to dump the
// experimental vendor registers once. Output is one line per poll.

#include <Wire.h>

constexpr uint8_t  PIN_SDA = 4;               // D2
constexpr uint8_t  PIN_SCL = 5;               // D1
constexpr uint32_t BUS_HZ = 100000;
constexpr uint32_t STRETCH_LIMIT_US = 100000;
constexpr uint16_t GAP_MS = 20;               // spec for the 2000BP says >= 15 ms between transactions
constexpr uint32_t POLL_MS = 2000;
constexpr uint8_t  MAX_FAILS_BEFORE_RECOVERY = 3;

// Alarm thresholds (edit to taste)
constexpr float VIN_MIN = 90.0f, VIN_MAX = 264.0f;   // data sheet input range
constexpr float TEMP_WARN_C = 70.0f;
constexpr float FAN_MIN_RPM = 500.0f;                // below this the fan is considered stopped

uint8_t psu = 0;        // 7-bit address
bool csvOut = false;
uint32_t okReads = 0, pecErrors = 0, busErrors = 0;
uint8_t consecutiveFails = 0;

// ---------- PEC and low-level reads ----------
uint8_t crc8(const uint8_t *d, uint8_t n) {  // SMBus PEC: poly 0x07, init 0
  uint8_t c = 0;
  for (uint8_t i = 0; i < n; i++) {
    c ^= d[i];
    for (uint8_t k = 0; k < 8; k++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

// Read `n` data bytes of `cmd` plus the PEC byte, verify the PEC. Returns true when valid.
bool readPec(uint8_t cmd, uint8_t *data, uint8_t n) {
  delay(GAP_MS);
  Wire.beginTransmission(psu);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) { busErrors++; return false; }
  uint8_t want = n + 1;
  if (Wire.requestFrom(psu, want) != want) { busErrors++; return false; }
  uint8_t m[3 + 8] = {(uint8_t)(psu << 1), cmd, (uint8_t)((psu << 1) | 1)};
  for (uint8_t i = 0; i < n; i++) { data[i] = Wire.read(); m[3 + i] = data[i]; }
  uint8_t pec = Wire.read();
  if (crc8(m, 3 + n) != pec) { pecErrors++; return false; }
  okReads++;
  return true;
}

bool readByteC(uint8_t cmd, uint8_t &v) { return readPec(cmd, &v, 1); }

bool readWordC(uint8_t cmd, uint16_t &v) {
  uint8_t b[2];
  if (!readPec(cmd, b, 2)) return false;
  v = b[0] | (b[1] << 8);
  return true;
}

// SMBus block read with PEC: [len][data...][PEC]. Fills `out` (NUL terminated), returns length or -1.
int readBlockPec(uint8_t cmd, char *out, uint8_t maxLen) {
  delay(GAP_MS);
  Wire.beginTransmission(psu);
  Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) { busErrors++; return -1; }
  uint8_t ask = maxLen + 2;                       // length byte + data + PEC
  uint8_t got = Wire.requestFrom(psu, ask);
  if (got < 2) { busErrors++; return -1; }
  uint8_t buf[40];
  for (uint8_t i = 0; i < got && i < sizeof(buf); i++) buf[i] = Wire.read();
  uint8_t len = buf[0];
  if (len > maxLen || len + 2 > got) { pecErrors++; return -1; }
  uint8_t m[3 + 36] = {(uint8_t)(psu << 1), cmd, (uint8_t)((psu << 1) | 1)};
  for (uint8_t i = 0; i < len + 1; i++) m[3 + i] = buf[i];
  if (crc8(m, 3 + len + 1) != buf[len + 1]) { pecErrors++; return -1; }
  for (uint8_t i = 0; i < len; i++) out[i] = (buf[1 + i] >= 32 && buf[1 + i] < 127) ? buf[1 + i] : '?';
  out[len] = 0;
  while (len && out[len - 1] == ' ') out[--len] = 0;  // trim padding
  okReads++;
  return len;
}

float linear11(uint16_t raw) {
  int exp = raw >> 11;       if (exp > 15) exp -= 32;
  int man = raw & 0x7FF;     if (man > 1023) man -= 2048;
  return man * powf(2.0f, exp);
}

// ---------- bus management ----------
void beginWire() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(BUS_HZ);
  Wire.setClockStretchLimit(STRETCH_LIMIT_US);
}

void recoverBus() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(PIN_SDA) == LOW; i++) {
    digitalWrite(PIN_SCL, LOW);  delayMicroseconds(10);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(10);
  }
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SDA, LOW);  delayMicroseconds(10);
  digitalWrite(PIN_SCL, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_SDA, HIGH); delayMicroseconds(10);
  beginWire();
}

bool findPsu() {
  for (uint8_t a = 0x68; a <= 0x6F; a++) {      // ADDRESS pin range: D0..DE >> 1
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { psu = a; return true; }
    delay(2);
  }
  psu = 0;
  return false;
}

// ---------- reporting ----------
void printIdentity() {
  char s[40];
  Serial.println(F("# identity"));
  const struct { uint8_t cmd; const char *name; uint8_t max; } ids[] = {
    {0x99, "MFR_ID", 8}, {0x9A, "MFR_MODEL", 20}, {0x9B, "MFR_REVISION", 20},
    {0x9C, "MFR_LOCATION", 12}, {0x9E, "MFR_SERIAL", 12},
  };
  for (const auto &i : ids) {
    if (readBlockPec(i.cmd, s, i.max) >= 0) Serial.printf("# %s=%s\n", i.name, s);
    else Serial.printf("# %s=unreadable\n", i.name);
  }
  uint8_t a = 0, b = 0;
  if (readByteC(0xFB, a) && readByteC(0xFC, b)) Serial.printf("# FW primary=0x%02X secondary=0x%02X\n", a, b);
  if (readByteC(0x02, a)) Serial.printf("# ON_OFF_CONFIG=0x%02X\n", a);
  Serial.printf("# PSU 7-bit 0x%02X (8-bit 0x%02X)\n", psu, psu << 1);
  if (csvOut) Serial.println(F("ms,vin_v,temp1_c,temp2_c,fan_rpm,pin_w,pin_int_w,vendor6F,st_vout,st_iout,st_in,st_temp,st_cml,st_mfr,st_fan,alarms,ok,pec_err,bus_err"));
}

void dumpVendor() {  // one-shot dump of the unidentified vendor registers (read-only)
  Serial.println(F("# vendor registers (meaning unknown)"));
  const uint8_t words[] = {0x6C, 0x6E, 0x6F, 0xE1, 0xBA, 0xBB, 0xBC, 0xBD, 0xDE, 0xA8, 0xB0, 0xE3};
  for (uint8_t c : words) {
    uint16_t v;
    if (readWordC(c, v)) Serial.printf("# 0x%02X=0x%04X (%u)\n", c, v, v);
    else Serial.printf("# 0x%02X=unreadable\n", c);
  }
  const uint8_t blocks[] = {0x70, 0x71, 0x72};
  for (uint8_t c : blocks) {
    uint8_t b[6];
    if (readPec(c, b, 5)) {   // 0x70..0x72 answer with len=4 then 4 data bytes
      Serial.printf("# 0x%02X=", c);
      for (uint8_t i = 1; i < 5; i++) Serial.printf("%02X", b[i]);
      Serial.println();
    } else Serial.printf("# 0x%02X=unreadable\n", c);
  }
}

void poll() {
  uint16_t vin = 0, t1 = 0, t2 = 0, fan = 0, pin = 0, pinInt = 0, v6f = 0;
  uint8_t sVout = 0, sIout = 0, sIn = 0, sTemp = 0, sCml = 0, sMfr = 0, sFan = 0;
  bool good = true;

  good &= readWordC(0x88, vin);
  good &= readWordC(0x8D, t1);
  good &= readWordC(0x8E, t2);
  good &= readWordC(0x90, fan);
  good &= readWordC(0x97, pin);
  bool haveInt = readWordC(0x6E, pinInt);
  bool have6F = readWordC(0x6F, v6f);
  bool haveStatus = readByteC(0x7A, sVout) & readByteC(0x7B, sIout) & readByteC(0x7C, sIn) &
                    readByteC(0x7D, sTemp) & readByteC(0x7E, sCml) & readByteC(0x80, sMfr) &
                    readByteC(0x81, sFan);

  if (!good) {
    consecutiveFails++;
    Serial.printf("# poll failed (%u in a row) ok=%lu pec_err=%lu bus_err=%lu\n", consecutiveFails,
                  (unsigned long)okReads, (unsigned long)pecErrors, (unsigned long)busErrors);
    if (consecutiveFails >= MAX_FAILS_BEFORE_RECOVERY) {
      Serial.println(F("# recovering bus and rescanning"));
      recoverBus();
      delay(200);
      if (findPsu()) { Serial.printf("# PSU found at 0x%02X\n", psu); consecutiveFails = 0; }
      else Serial.println(F("# PSU not answering (check pull-ups, AC, wiring)"));
    }
    return;
  }
  consecutiveFails = 0;

  float vinV = linear11(vin), temp1 = linear11(t1), temp2 = linear11(t2);
  float rpm = linear11(fan), pinW = linear11(pin);

  char alarms[64] = "";
  if (vinV < VIN_MIN || vinV > VIN_MAX) strcat(alarms, "VIN_RANGE ");
  if (temp1 > TEMP_WARN_C || temp2 > TEMP_WARN_C) strcat(alarms, "TEMP_HIGH ");
  if (rpm < FAN_MIN_RPM) strcat(alarms, "FAN_LOW ");
  if (haveStatus && (sVout | sIout | sIn | sTemp | sFan)) strcat(alarms, "STATUS ");
  if (!alarms[0]) strcpy(alarms, "-");

  if (csvOut) {
    Serial.printf("%lu,%.2f,%.1f,%.1f,%.0f,%.1f,%d,%d,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%s,%lu,%lu,%lu\n",
                  (unsigned long)millis(), vinV, temp1, temp2, rpm, pinW,
                  haveInt ? (int)pinInt : -1, have6F ? (int)v6f : -1,
                  sVout, sIout, sIn, sTemp, sCml, sMfr, sFan, alarms,
                  (unsigned long)okReads, (unsigned long)pecErrors, (unsigned long)busErrors);
  } else {
    Serial.printf("VIN %.2f V | T1 %.1f C | T2 %.1f C | fan %.0f rpm | PIN %.1f W | status V%02X I%02X In%02X T%02X CML%02X M%02X F%02X | alarms %s\n",
                  vinV, temp1, temp2, rpm, pinW, sVout, sIout, sIn, sTemp, sCml, sMfr, sFan, alarms);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  beginWire();
  Serial.println(F("\n# CSV750BP PMBus monitor (read-only)"));
  while (!findPsu()) {
    Serial.println(F("# no PSU at 0x68..0x6F, retrying (check pull-ups, AC, wiring)"));
    delay(2000);
  }
  printIdentity();
}

void loop() {
  static uint32_t last = 0;
  if (Serial.available()) {
    int ch = Serial.read();
    delay(50);
    while (Serial.available()) Serial.read();
    if (ch == 'c' || ch == 'C') { csvOut = !csvOut; printIdentity(); }
    else if (ch == 'r' || ch == 'R') printIdentity();
    else if (ch == 'x' || ch == 'X') dumpVendor();
  }
  if (millis() - last >= POLL_MS) {
    last = millis();
    poll();
  }
}
