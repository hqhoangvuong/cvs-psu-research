// Read-only PMBus probe for the Artesyn CSV750BP on an ESP8266 (Arduino core).
//
// Wiring (all grounds common; ESP8266 is 3.3 V logic, never connect 5 V):
//   ESP8266 D2 (GPIO4) -> PSU S11 SDA
//   ESP8266 D1 (GPIO5) -> PSU S12 SCL
//   ESP8266 GND        -> PSU S10 RETURN (signal return; also RTN pins)
//   SDA and SCL each need a pull-up to 3.3 V (about 2.2 kOhm). The PSU has none.
//   PSU S15 ADDRESS: leave open for 8-bit 0xD0 (7-bit 0x68), or use the datasheet resistor to GND.
//   PSU needs AC applied and PSON asserted to be fully up; the datasheet's wording on
//   whether I2C answers on standby alone is unconfirmed for the 750BP, so record what you see.
//
// This sketch only reads. It never writes OPERATION, CLEAR_FAULTS or any setting.
// Open the serial monitor at 115200 baud. Send any character to run the probe again.
// Paste the whole output back; it is formatted to be turned into the PMBus document.
// A "variants" block tests read length (1-4 bytes) and repeated-start vs STOP framing per command.
// Record the PSU state (main output on/off, load) with each capture.

#include <Wire.h>

constexpr uint8_t PIN_SDA = 4;  // D2
constexpr uint8_t PIN_SCL = 5;  // D1
constexpr uint32_t BUS_HZ = 100000;          // guaranteed speed per CSV2000BP note
constexpr uint32_t STRETCH_LIMIT_US = 100000; // 100 ms clock stretch max per CSV2000BP note
constexpr uint16_t GAP_MS = 20;              // note requires >= 15 ms between transactions

uint8_t psu = 0;  // 7-bit address

void gap() { delay(GAP_MS); }

// Command byte, then repeated start, then read n bytes. Returns bytes read, or -1 on NACK/error.
int readBytes(uint8_t cmd, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(psu);
  Wire.write(cmd);
  uint8_t err = Wire.endTransmission(false);
  if (err) { gap(); return -1; }
  uint8_t got = Wire.requestFrom(psu, n);
  for (uint8_t i = 0; i < got; i++) buf[i] = Wire.read();
  gap();
  return got == n ? got : -1;
}

// SMBus block read: first byte returned is the length.
int readBlock(uint8_t cmd, uint8_t *buf, uint8_t maxLen) {
  Wire.beginTransmission(psu);
  Wire.write(cmd);
  if (Wire.endTransmission(false)) { gap(); return -1; }
  uint8_t got = Wire.requestFrom(psu, (uint8_t)(maxLen + 1));
  if (got < 1) { gap(); return -1; }
  uint8_t len = Wire.read();
  if (len > maxLen) len = maxLen;
  for (uint8_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
  while (Wire.available()) Wire.read();
  gap();
  return len;
}

// PMBus QUERY (0x1A): block write 1 byte (the command), block read 1 byte of support info.
// Returns the info byte, or -1 if QUERY itself fails.
int query(uint8_t cmd) {
  Wire.beginTransmission(psu);
  Wire.write(0x1A);
  Wire.write(1);
  Wire.write(cmd);
  if (Wire.endTransmission(false)) { gap(); return -1; }
  uint8_t got = Wire.requestFrom(psu, (uint8_t)2);
  if (got < 2) { gap(); return -1; }
  uint8_t len = Wire.read();
  uint8_t info = Wire.read();
  gap();
  return len == 1 ? info : -1;
}

float linear11(uint16_t raw) {
  int exp = raw >> 11;       if (exp > 15) exp -= 32;
  int man = raw & 0x7FF;     if (man > 1023) man -= 2048;
  return man * powf(2.0f, exp);
}

float linear16(uint16_t raw, int exp) { return raw * powf(2.0f, exp); }

void scanBus() {
  Serial.println(F("# scan 7-bit addresses 0x08..0x77"));
  bool found = false;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("found,0x%02X,8bit_write=0x%02X\n", a, a << 1);
      found = true;
    }
    delay(2);
  }
  if (!found) Serial.println(F("# nothing answered: check pull-ups, GND, PSU power/PSON, SDA/SCL swap"));
}

void printWord(const char *name, uint8_t cmd) {
  uint8_t b[2];
  if (readBytes(cmd, b, 2) < 0) { Serial.printf("read,0x%02X,%s,NACK\n", cmd, name); return; }
  uint16_t raw = b[0] | (b[1] << 8);
  Serial.printf("read,0x%02X,%s,raw=0x%04X,linear11=%.4f\n", cmd, name, raw, linear11(raw));
}

void printStatusByte(const char *name, uint8_t cmd) {
  uint8_t b;
  if (readBytes(cmd, &b, 1) < 0) { Serial.printf("read,0x%02X,%s,NACK\n", cmd, name); return; }
  Serial.printf("read,0x%02X,%s,raw=0x%02X\n", cmd, name, b);
}

void printBlock(const char *name, uint8_t cmd) {
  uint8_t b[33] = {0};
  int n = readBlock(cmd, b, 32);
  if (n < 0) { Serial.printf("read,0x%02X,%s,NACK\n", cmd, name); return; }
  Serial.printf("read,0x%02X,%s,len=%d,hex=", cmd, name, n);
  for (int i = 0; i < n; i++) Serial.printf("%02X", b[i]);
  Serial.print(F(",ascii="));
  for (int i = 0; i < n; i++) Serial.print((b[i] >= 32 && b[i] < 127) ? (char)b[i] : '.');
  Serial.println();
}

// Raw read for framing experiments: command write, then either repeated start (stop=false) or
// STOP before the read (stop=true), then n bytes. Returns bytes received or -1 on write error.
int readRaw(uint8_t cmd, uint8_t n, bool stop, uint8_t *buf) {
  Wire.beginTransmission(psu);
  Wire.write(cmd);
  uint8_t err = Wire.endTransmission(stop);
  if (err) { gap(); return -1; }
  uint8_t got = Wire.requestFrom(psu, n);
  for (uint8_t i = 0; i < got; i++) buf[i] = Wire.read();
  gap();
  return got;
}

void printRaw(uint8_t cmd, const char *name, uint8_t n, bool stop) {
  uint8_t b[8] = {0};
  int got = readRaw(cmd, n, stop, b);
  Serial.printf("variant,0x%02X,%s,%s,n=%u,", cmd, name, stop ? "stop" : "rstart", n);
  if (got < 0) { Serial.println(F("write_err")); return; }
  Serial.printf("got=%d,hex=", got);
  for (int i = 0; i < got; i++) Serial.printf("%02X", b[i]);
  Serial.println();
}

// Framing/length experiments on the commands that read back with a 0xFF high byte, plus READ_VIN
// (known good) as a control. Compare: does the 1-byte read give the same low byte, does a 3rd/4th
// byte show PEC or data, does STOP-before-read change the high byte, are repeats stable.
void variants() {
  Serial.println(F("# variants: variant,cmd,name,framing,n,got,hex"));
  const struct { uint8_t cmd; const char *name; } cmds[] = {
    {0x88, "READ_VIN"}, {0x89, "READ_IIN"}, {0x8B, "READ_VOUT"}, {0x8C, "READ_IOUT"},
    {0x8F, "READ_TEMPERATURE_3"}, {0x96, "READ_POUT"}, {0x97, "READ_PIN"},
    {0xA6, "MFR_IOUT_MAX"}, {0xC0, "MFR_MAX_TEMP_1"}, {0x79, "STATUS_WORD"}, {0x78, "STATUS_BYTE"},
  };
  for (const auto &c : cmds) {
    for (uint8_t n = 1; n <= 4; n++) printRaw(c.cmd, c.name, n, false);  // repeated start
    for (uint8_t n = 2; n <= 3; n++) printRaw(c.cmd, c.name, n, true);   // STOP before read
    for (uint8_t i = 0; i < 3; i++) printRaw(c.cmd, c.name, 2, false);   // stability
  }
}

void probe() {
  scanBus();

  // Default address with S15 open is 8-bit 0xD0 = 7-bit 0x68. Try the whole 0x68..0x6F range.
  psu = 0;
  for (uint8_t a = 0x68; a <= 0x6F; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { psu = a; break; }
  }
  if (!psu) { Serial.println(F("# no PSU at 0x68..0x6F, stopping")); return; }
  Serial.printf("# using 7-bit 0x%02X (8-bit 0x%02X)\n", psu, psu << 1);

  Serial.println(F("# identity"));
  printBlock("MFR_ID", 0x99);
  printBlock("MFR_MODEL", 0x9A);
  printBlock("MFR_REVISION", 0x9B);
  printBlock("MFR_LOCATION", 0x9C);
  printBlock("MFR_DATE", 0x9D);
  printBlock("MFR_SERIAL", 0x9E);
  printStatusByte("PMBUS_REVISION", 0x98);
  printStatusByte("CAPABILITY", 0x19);
  printStatusByte("VOUT_MODE", 0x20);
  printStatusByte("ON_OFF_CONFIG", 0x02);
  printStatusByte("OPERATION", 0x01);
  printStatusByte("PRIMARY_FW_REVISION", 0xFB);
  printStatusByte("SECONDARY_FW_VERSION", 0xFC);

  // Resolve READ_VOUT format: show LINEAR11 and LINEAR16 (using VOUT_MODE exponent).
  uint8_t vm = 0;
  int vmOk = readBytes(0x20, &vm, 1);
  int vexp = (vm & 0x1F); if (vexp > 15) vexp -= 32;
  uint8_t v[2];
  if (vmOk >= 0 && readBytes(0x8B, v, 2) >= 0) {
    uint16_t raw = v[0] | (v[1] << 8);
    Serial.printf("vout,raw=0x%04X,as_linear11=%.4f,as_linear16=%.4f,vout_mode=0x%02X,exp=%d\n",
                  raw, linear11(raw), linear16(raw, vexp), vm, vexp);
  } else {
    Serial.println(F("vout,NACK"));
  }

  Serial.println(F("# status"));
  printWord("STATUS_WORD", 0x79);
  printStatusByte("STATUS_BYTE", 0x78);
  printStatusByte("STATUS_VOUT", 0x7A);
  printStatusByte("STATUS_IOUT", 0x7B);
  printStatusByte("STATUS_INPUT", 0x7C);
  printStatusByte("STATUS_TEMPERATURE", 0x7D);
  printStatusByte("STATUS_CML", 0x7E);
  printStatusByte("STATUS_MFR_SPECIFIC", 0x80);
  printStatusByte("STATUS_FANS_1_2", 0x81);

  Serial.println(F("# telemetry (decoded as LINEAR11 unless noted above)"));
  printWord("READ_VIN", 0x88);
  printWord("READ_IIN", 0x89);
  printWord("READ_IOUT", 0x8C);
  printWord("READ_TEMPERATURE_1", 0x8D);
  printWord("READ_TEMPERATURE_2", 0x8E);
  printWord("READ_TEMPERATURE_3", 0x8F);
  printWord("READ_FAN_SPEED_1", 0x90);
  printWord("READ_POUT", 0x96);
  printWord("READ_PIN", 0x97);
  printWord("MFR_IOUT_MAX", 0xA6);
  printWord("MFR_POUT_MAX", 0xA7);
  printWord("MFR_MAX_TEMP_1", 0xC0);
  printWord("MFR_MAX_TEMP_2", 0xC1);
  printWord("MFR_MAX_TEMP_3", 0xC2);

  variants();

  // QUERY every command code. info bit7 = supported, bit6 = write, bit5 = read,
  // bits4:2 = data format code (see PMBus Part II, QUERY). Raw byte is printed; decode offline.
  Serial.println(F("# QUERY sweep: query,cmd,info"));
  int first = query(0x00);
  if (first < 0) {
    Serial.println(F("# QUERY not supported or failed (this PSU ACKs every command code, so no ACK map)"));
  } else {
    for (int c = 0; c < 256; c++) {
      int info = query((uint8_t)c);
      if (info >= 0 && (info & 0x80)) Serial.printf("query,0x%02X,0x%02X\n", c, info);
    }
  }
  Serial.println(F("# DONE"));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(BUS_HZ);
  Wire.setClockStretchLimit(STRETCH_LIMIT_US);
  Serial.println(F("\n# CSV750BP PMBus probe (read-only)"));
  probe();
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    probe();
  }
}
