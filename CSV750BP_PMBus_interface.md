# CSV750BP PMBus / I2C interface (draft 1)

Unit tested: CSV750BP-3 (MFR_MODEL `00YL557`, MFR_REVISION `00YL556`, MFR_ID `EMER`, serial `67M2P9`, firmware bytes 0xFB = 0x35, 0xFC = 0x30). Advanced Energy publishes no PMBus document for this part. This one is built from bench measurements (ESP8266, `firmware/pmbus_probe/`) with the CSV2000BP note as a fallback.

**Status key**

| Tag | Meaning |
|---|---|
| **M** | Measured on the CSV750BP |
| **D** | From the CSV750BP data sheet |
| **I** | Inferred from the CSV2000BP note, not confirmed for the 750BP |
| **?** | Unknown |

Raw captures and analysis: `notes/probe_run1_analysis.md`, `notes/pmbus_commands_csv750bp_measured.csv`.

## 1. Key finding

The 750BP does **not** implement the CSV2000BP command table. With the main output on (12.33 V at the output, no load), `READ_VOUT`, `READ_IOUT`, `READ_IIN`, `READ_POUT`, `READ_TEMPERATURE_3`, `STATUS_BYTE`, `STATUS_WORD`, `CAPABILITY`, `PMBUS_REVISION`, `MFR_IOUT_MAX` and `MFR_MAX_TEMP_1..3` return a stale single byte with no PEC. The unit then sets the invalid-command bit in `STATUS_CML`. Do not trust the 2000BP table for the 750BP. Use only the commands in section 5. **M**

## 2. Connector signals

| Signal | Pin | Source |
|---|---|---|
| SDA | S11 | D |
| SCL | S12 | D |
| ADDRESS | S15 | D |
| SMBUS_RESET | S13 | D |
| SMB_ALERT# | S8 | D |
| PSON | S16, S17 | D |
| PWOK | S6 | D |
| PRESENT | S7 | D |
| RETURN (signal ground) | S10 | D |

On the bench PSON was hardwired asserted, so the main output starts when AC is applied.

## 3. Bus and addressing

| Item | Value | Status |
|---|---|---|
| Address, ADDRESS open | 8-bit `0xD0`, 7-bit `0x68` | **M** |
| Address scheme | 8-bit D0 to DE, step 2, set by a resistor from S15 to return | D |
| Address resistor values | Open D0, 280k D2, **121k** D4, 68.1k D6, 40.2k D8, 23.7k DA, 13.3k DC, 5.76k DE | D (the data sheet prints 212k for D4, a typo; divider math gives 121k) |
| Pull-ups | The PSU has none. Use about 2.2 kΩ to 3.3 V on SDA and SCL | **M** |
| 10 kΩ pull-ups | Failed: the PSU stopped acknowledging addresses part-way through a run (after roughly 50 transactions). It did not recur with 2.2 kΩ | **M** |
| Speed | 100 kHz worked | **M** (limit I) |
| Gap between transactions | 20 ms used; the 2000BP note requires at least 15 ms | I |
| Clock stretching | Stretch limit set to 100 ms in the sketch; the actual stretch time was not measured | **M** (limit I) |
| Framing | Command write, then repeated start, then read; STOP before the read also works | **M** |
| Answers on standby alone | Not tested: PSON was hardwired asserted, so the main output was always on | ? |
| ESP8266 note | Bit-banged I2C; set `Wire.setClockStretchLimit()` | **M** |

## 4. PEC

The PSU appends a PEC (SMBus CRC-8, polynomial 0x07, initial 0) to the data of every command it implements. For a read the CRC covers: 8-bit write address, command, 8-bit read address, then all data bytes. Example: `READ_VIN` bytes `89 F3 74` have data `89 F3` and PEC `0x74`. Extra bytes read past the PEC come back as `0xFF`. **M**

A reply with no valid PEC means the command is not implemented (a stale byte, then `0xFF`). This is how the command support map in section 5 was built, since `QUERY` (0x1A) does not work. **M**

## 5. Commands

### 5.1 Implemented and decoded (PEC valid)

| Code | Name | Reads | Format / meaning | Status |
|---|---|---|---|---|
| 0x01 | OPERATION | 0x00 | Byte. Ignored for turning the output on or off (see ON_OFF_CONFIG) | **M** |
| 0x02 | ON_OFF_CONFIG | 0x15 | Byte: bit4 = 1, bit3 = 0 (serial OPERATION ignored), bit2 = 1 (CONTROL pin used), bit1 = 0 (active low), bit0 = 1 (immediate off). The 2000BP default is 0x1C | **M**, bit meanings I |
| 0x20 | VOUT_MODE | 0x1B | Linear, exponent −5. Not useful: `READ_VOUT` is not implemented | **M** |
| 0x7A | STATUS_VOUT | 0x00 | Byte | **M** |
| 0x7B | STATUS_IOUT | 0x00 | Byte | **M** |
| 0x7C | STATUS_INPUT | 0x00 | Byte | **M** |
| 0x7D | STATUS_TEMPERATURE | 0x00 | Byte | **M** |
| 0x7E | STATUS_CML | 0x00 (0x82 after unsupported-command probing) | Bit7 invalid command, bit1 other comm fault. Cleared by reading in practice | **M** |
| 0x80 | STATUS_MFR_SPECIFIC | 0x00 | Byte | **M** |
| 0x81 | STATUS_FANS_1_2 | 0x00 | Byte | **M** |
| 0x88 | READ_VIN | 225.75 to 227 V | LINEAR11, 0.25 V steps. Example `F3 89` → 226.25 V | **M** |
| 0x8D | READ_TEMPERATURE_1 | 22 to 27 °C | LINEAR11 | **M** |
| 0x8E | READ_TEMPERATURE_2 | 30 to 31 °C | LINEAR11 | **M** |
| 0x90 | READ_FAN_SPEED_1 | about 5980 to 6770 rpm | LINEAR11 (exponent +3) | **M** |
| 0x97 | READ_PIN | 2 to 5 W at no load | LINEAR11. Idle input power, resolution 0.5 W | **M** |
| 0xA7 | MFR_POUT_MAX | 750 | LINEAR11, watts | **M** |
| 0x99 | MFR_ID | `EMER` | Block, ASCII | **M** |
| 0x9A | MFR_MODEL | `00YL557` | Block, 12 bytes, space padded | **M** |
| 0x9B | MFR_REVISION | `00YL556` | Block, 12 bytes | **M** |
| 0x9C | MFR_LOCATION | ` K1181` | Block, 6 bytes | **M** |
| 0x9E | MFR_SERIAL | `67M2P9` | Block, 6 bytes | **M** |
| 0x9D | MFR_DATE | 30 bytes of 0xFF | Block, not programmed | **M** |
| 0xFB | PRIMARY_FW_REVISION | 0x35 | Byte | **M** |
| 0xFC | SECONDARY_FW_VERSION | 0x30 | Byte | **M** |
| 0x30 | COEFFICIENTS | `01 00 00 00 00` | 5-byte block, meaning unknown | **M**, decode ? |
| 0x3A | (FAN_CONFIG_1_2?) | 0xD0 | Byte | **M**, name I |
| 0x3B | (FAN_COMMAND_1?) | `0x0BE8` | Word | **M**, meaning ? |

### 5.2 Implemented, meaning not yet identified (vendor codes)

Values are at no load with the main output on.

| Code | Reads | Notes |
|---|---|---|
| 0x6C | `0xCA80` | Constant; equals READ_PIN at a snapshot (5.0 W) |
| 0x6E | 5, 6, 7 | Integer watts; tracks READ_PIN |
| 0x6F | 0 | Possibly an integer POUT or IOUT. Needs a load to confirm |
| 0x70 | block `60/72 01 FF 02` | 4 bytes, first byte flickers |
| 0x71 | block `00 00 12 00` | Second byte flickers 00/01 |
| 0x72 | block `FB/FD D2/FE 9E 9A` | Noisy |
| 0x83, 0x84, 0xBE, 0xD9, 0xDA | 0x0C, 0, 0, 0, 0 | Static |
| 0xA8, 0xB0 | 750 | Static |
| 0xB9 | 1 | Static |
| 0xBA | 81 | Static |
| 0xBB, 0xBC | 900 | Static |
| 0xBD | `0x1D2B` | Static |
| 0xDE | `0x017A` | Static |
| 0xDF | 0xFB | Static |
| 0xE1 | LINEAR11 5.0, 6.0, 7.0 W | Tracks READ_PIN |
| 0xE3 | `0x0101` | Static |
| 0xFA, 0xFD, 0xFE | 0x30, 0x3B, 0x03 | Static (firmware bytes?) |

### 5.3 Not implemented on this unit

`CAPABILITY` (0x19), `PMBUS_REVISION` (0x98), `STATUS_BYTE` (0x78), `STATUS_WORD` (0x79), `READ_IIN` (0x89), `READ_VOUT` (0x8B), `READ_IOUT` (0x8C), `READ_TEMPERATURE_3` (0x8F), `READ_POUT` (0x96), `MFR_IOUT_MAX` (0xA6), `MFR_MAX_TEMP_1..3` (0xC0 to 0xC2), `QUERY` (0x1A). Values read from them (for example `CAPABILITY` 0xFC, `PMBUS_REVISION` 0x5F) are stale bytes, not data. **M**

### 5.4 Not tested

All other command codes in the CSV2000BP table (for example `CLEAR_FAULTS` 0x03; `READ_EIN` 0x86 and `READ_EOUT` 0x87 did not show up in the sweep, so are probably not implemented) were not tested for writes. Blocks longer than six data bytes were not covered by the PEC sweep. The probe is read-only. **?**

## 6. Output current, power and voltage

No PMBus register for output current, power or voltage has been identified. At no load, `0x6F` reads 0 and `0x6E`/`0xE1` track input power. A load test is needed to confirm which vendor code carries output current or power. Output voltage was measured only with a multimeter (12.32 to 12.33 V). **?**

## 7. Open items

1. Identify the output measurement registers with a load (candidates: 0x6F, 0x70 to 0x72).
2. Ask Advanced Energy support (productsupport.ep@aei.com) for the 00YL557 command list.
3. Test writes (`CLEAR_FAULTS` and the fan commands) only if wanted; the probe does none.
4. Measure SMB_ALERT#, PWOK and PRESENT electrically; not yet done.
5. Confirm the FRU EEPROM address, if any.
