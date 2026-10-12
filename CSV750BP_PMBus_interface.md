# CSV750BP PMBus / I2C interface

Unit measured: CSV750BP-3 with MFR_MODEL `00YL557`, MFR_REVISION `00YL556`, MFR_ID `EMER`, serial `67M2P9`, firmware bytes 0xFB = 0x35 and 0xFC = 0x30.

Advanced Energy publishes no PMBus document for the CSV750BP. Its data sheet has no command list. This document is built from bench measurements on one unit (ESP8266 with `firmware/pmbus_probe/`), with the CSV750BP data sheet and the CSV2000BP technical reference note as fallbacks. Other units or firmware versions may differ.

**Status key**

| Tag | Meaning |
|---|---|
| **M** | Measured on the CSV750BP |
| **D** | From the CSV750BP data sheet |
| **I** | Inferred from the CSV2000BP note, not confirmed for the 750BP |
| **?** | Unknown |

Raw captures and the reasoning: `notes/probe_run1_analysis.md` and `notes/pmbus_commands_csv750bp_measured.csv`.

## 1. Summary

- The unit answers at 7-bit address `0x68` (8-bit `0xD0`) with ADDRESS open. It needs about 2.2 kΩ pull-ups on SDA and SCL. 10 kΩ made it stop answering. **M**
- Every command it implements returns its data followed by an SMBus **PEC byte**. A command with no valid PEC is **not implemented**: the unit returns one stale byte, then `0xFF`. **M**
- It implements only part of the CSV2000BP command table. **No output voltage, output power or status word is available over PMBus**, and output current exists only as whole amps in a vendor register. **M**
- Available: input voltage, input power, input energy, two temperatures, fan speed, identity strings, the individual status bytes and a few vendor registers.
- Output voltage, current and power need an external meter (INA260 or INA226), or the manufacturer's command list for part 00YL557.

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
| +Vsense / −Vsense | S3 / S22 | D (remote sense; compensation limit is I, about 500 mV) |
| Standby output VSB | P19, P20 | D (12.0 V nominal, 0 to 2.5 A, always on while AC is present) |

On the bench PSON was hardwired asserted, so the main output starts when AC is applied. Remote sense was left unconnected.

## 3. Bus and addressing

| Item | Value | Status |
|---|---|---|
| Address, ADDRESS open | 8-bit `0xD0`, 7-bit `0x68` | **M** |
| Address scheme | 8-bit D0 to DE in steps of 2, set by a resistor from S15 to return. Use the 7-bit value (8-bit >> 1) in I2C libraries | D |
| Address resistors | Open D0, 280k D2, **121k** D4, 68.1k D6, 40.2k D8, 23.7k DA, 13.3k DC, 5.76k DE | D. The data sheet prints 212k for D4, a typo: divider math and the CSV2000BP note give 121k |
| Pull-ups | None inside the PSU. Use about 2.2 kΩ to 3.3 V on SDA and SCL | **M** |
| 10 kΩ pull-ups | Failed: the PSU stopped acknowledging addresses part-way through a run, after about 50 transactions. Did not recur with 2.2 kΩ | **M** |
| Speed | 100 kHz worked | **M** (limit I) |
| Gap between transactions | 20 ms used; the CSV2000BP note requires at least 15 ms | I |
| Clock stretching | 100 ms limit set in the sketch; the actual stretch time was not measured | I |
| Read framing | Write the command, then a repeated start (or STOP then START) and read immediately | **M** |
| Response window | The unit answers only within a few ms of the command write. With a delay of 10 ms or more, or a second read without re-sending the command, every command returned `FF FF FF` | **M** |
| Answers on standby alone | Not tested: PSON was hardwired asserted | ? |
| ESP8266 | Set `Wire.setClockStretchLimit()`; use the repeated-start read | **M** |

## 4. PEC

The PEC is an SMBus CRC-8 (polynomial 0x07, initial value 0). For a read it covers, in order: the 8-bit write address, the command, the 8-bit read address, then every data byte (including the length byte for blocks).

Example, `READ_VIN` (0x88) at 8-bit address `0xD0`: reading three bytes returns `8D F3 20`. The data is `8D F3` and the PEC is `0x20`, which equals the CRC of `D0 88 D1 8D F3`. Reading further bytes returns `0xFF`.

Reads for single-byte commands return 2 bytes (data, PEC), words 3 bytes, blocks `len + 2` bytes. **M**

A reply whose last data byte is not a valid PEC means the command is not implemented. The command support map in section 5 was built this way, because `QUERY` (0x1A) does not work. **M**

## 5. Commands

### 5.1 Implemented and decoded (PEC valid)

| Code | Name | Typical reading | Format and meaning | Status |
|---|---|---|---|---|
| 0x01 | OPERATION | 0x00 | Byte. Does not turn the output on or off (see ON_OFF_CONFIG) | **M** |
| 0x02 | ON_OFF_CONFIG | 0x15 | bit4 = 1, bit3 = 0 (serial OPERATION ignored), bit2 = 1 (CONTROL pin used), bit1 = 0 (active low), bit0 = 1 (immediate). The 2000BP default is 0x1C | **M**, bit meanings I |
| 0x20 | VOUT_MODE | 0x1B | Linear mode, exponent −5. Not useful: `READ_VOUT` is not served | **M** |
| 0x30 | COEFFICIENTS | `01 00 00 00 00` | 5-byte block: m = 1 (bytes 1-2, LE), b = 0 (bytes 3-4), R = 0 (byte 5). DIRECT values equal their raw values | **M** |
| 0x3A | (FAN_CONFIG_1_2?) | 0xD0 | Byte | **M**, name I |
| 0x3B | (FAN_COMMAND_1?) | `0x0BE8` | Word | **M**, meaning ? |
| 0x7A | STATUS_VOUT | 0x00 | Byte | **M** |
| 0x7B | STATUS_IOUT | 0x00 | Byte | **M** |
| 0x7C | STATUS_INPUT | 0x00 | Byte | **M** |
| 0x7D | STATUS_TEMPERATURE | 0x00 | Byte | **M** |
| 0x7E | STATUS_CML | 0x00; 0x80 or 0x82 after unsupported commands | Bit 7 invalid command, bit 1 other communication fault. Reading appears to clear it | **M** |
| 0x80 | STATUS_MFR_SPECIFIC | 0x00 | Byte | **M** |
| 0x81 | STATUS_FANS_1_2 | 0x00 | Byte (family table: b7 fan 1 fault, b5 warning, b3 speed overridden) | **M**, bits I |
| 0x86 | READ_EIN | 6-byte block | Input energy, DIRECT m=1 b=0 R=0. Bytes 0-1 accumulator (LE, wraps at 32768), byte 2 rollover count, bytes 3-5 sample count (LE). Mean value per sample = Δ(rollover × 32768 + accumulator) / Δ(samples) = input power in W. About 14 samples/s | **M** |
| 0x87 | READ_EOUT | 6-byte block | Same layout, about 310 samples/s. Its mean per sample = the EIN mean + about 3 in every state tested (8.0 vs 5.0 at no load, 34.1 vs 31.0 at 31 W). It follows input power; do not use it as output power | **M** |
| 0x88 | READ_VIN | 225 to 229 V | LINEAR11, 0.25 V steps. Example `F3 8D` → exponent −2, mantissa 909 → 227.25 V | **M** |
| 0x8D | READ_TEMPERATURE_1 | 22 to 28 °C | LINEAR11 (ambient) | **M** |
| 0x8E | READ_TEMPERATURE_2 | 30 to 36 °C | LINEAR11 (hot spot); rises with load | **M** |
| 0x90 | READ_FAN_SPEED_1 | about 5980 to 6930 rpm | LINEAR11, exponent +3 | **M** |
| 0x97 | READ_PIN | 2 to 7 W idle; 19 to 36 W with a 20 to 30 W load | LINEAR11, 0.5 W resolution | **M** |
| 0xA7 | MFR_POUT_MAX | 750 | LINEAR11, watts | **M** |
| 0x99 | MFR_ID | `EMER` | Block, ASCII | **M** |
| 0x9A | MFR_MODEL | `00YL557` | Block, 12 bytes, space padded | **M** |
| 0x9B | MFR_REVISION | `00YL556` | Block, 12 bytes | **M** |
| 0x9C | MFR_LOCATION | ` K1181` | Block, 6 bytes | **M** |
| 0x9D | MFR_DATE | 30 × 0xFF | Block, not programmed | **M** |
| 0x9E | MFR_SERIAL | `67M2P9` | Block, 6 bytes | **M** |
| 0xFB | PRIMARY_FW_REVISION | 0x35 | Byte | **M** |
| 0xFC | SECONDARY_FW_VERSION | 0x30 | Byte | **M** |

### 5.2 Implemented, meaning only partly known (vendor codes)

| Code | Reading | Notes |
|---|---|---|
| 0x6F | word: 0 unplugged; 1 to 2 with 20 to 36 W input; 0 with the load off | **Output current in whole amps, truncated** (best fit: 0 at about 0.6 A, 1 at about 1.5 A, 2 at 2.2 to 2.6 A, never 3 up to 36 W input). Not confirmed with an ammeter. 1 A steps |
| 0x6E | word: 5 to 7 idle, 19 to 36 loaded | Input power as integer watts |
| 0x6C | word `CA80`, later `E200` etc. | Input power, LINEAR11, finer steps than READ_PIN |
| 0xE1 | word | Input power, LINEAR11 |
| 0x70 | 4-byte block, two words: 352 to 370 / 767 idle, 540 to 560 / 960 standby, 722 to 970 / 1150 to 1365 loaded | Rises with input power (about 22 to 25 counts per watt). Raw input-side values, not output |
| 0x71 | 4-byte block, second word 18 idle, 27 to 29 standby, 31 to 53 loaded | Rises with load, unidentified |
| 0x72 | 4-byte block `FB/FD D2/FE 9E/9A` | Noisy, does not follow load |
| 0x13, 0x14 | blocks of 14 and 21 bytes | Contain integer PIN, copies of the EIN counters and slowly drifting words. Not decoded |
| 0xD5 | block of 7 | Static `07 15 10 00 00 00 00` |
| 0x83, 0x84, 0xBE, 0xD9, 0xDA | 0x0C, 0, 0, 0, 0 | Static |
| 0xA8, 0xB0 | 750 | Static |
| 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xDE, 0xDF, 0xE3 | 1, 81, 900, 900, `0x1D2B`, `0x017A`, 0xFB, `0x0101` | Static |
| 0xFA, 0xFD, 0xFE | 0x30, 0x3B, 0x03 | Static (firmware bytes?) |

### 5.3 Not implemented (no valid PEC)

`CAPABILITY` (0x19), `PMBUS_REVISION` (0x98), `STATUS_BYTE` (0x78), `STATUS_WORD` (0x79), `READ_IIN` (0x89), `READ_VOUT` (0x8B), `READ_IOUT` (0x8C), `READ_TEMPERATURE_3` (0x8F), `READ_POUT` (0x96), `MFR_IOUT_MAX` (0xA6), `MFR_MAX_TEMP_1..3` (0xC0 to 0xC2), `PAGE` (0x00), `QUERY` (0x1A). **M**

They return one stale byte (for example `READ_VOUT` `0x37`, `READ_IOUT` `0x5C`, `PMBUS_REVISION` `0x5F`, `CAPABILITY` `0xFC`), then `0xFF`. The byte is the same with the load on or off and does not decode to a real value. These values, and the earlier "IIN −113.5 A" style readings, are artefacts, not data.

These all failed under every variation tried: repeated start and STOP framing, 1 to 4 and 36-byte reads, delays of 0 to 500 ms, a second read without re-sending the command, and writes of `PAGE` = 0 and 1. The `STATUS_CML` invalid-command bit is set after reading them. The CSV2000BP-family table lists all of them, so the family table lists more commands than firmware 00YL557 implements.

### 5.4 Not tested

- Writes other than `PAGE` (for example `CLEAR_FAULTS`, `OPERATION`, the fan commands, `VOUT_COMMAND`). The probe sends no other writes, and none are recommended without the manufacturer's information.
- Whether PMBus answers on standby alone (PSON deasserted).
- SMB_ALERT#, PWOK, PRESENT and SMBUS_RESET behaviour.
- A FRU EEPROM, if any.

## 6. Output voltage, current and power

| Quantity | Over PMBus | Status |
|---|---|---|
| Output voltage | Not available. `READ_VOUT` is not served. Measured only with a multimeter: 12.32 to 12.33 V at no load | **M** |
| Output current | Whole amps only, in vendor register 0x6F, probably truncated | **M**, meaning best fit |
| Output power | Not available. `READ_POUT` is not served. `READ_EOUT` follows input power | **M** |

For real readings use an INA260 (to 15 A) or an INA226 with an external shunt in the +12 V output line. `firmware/csv750_monitor/` supports either.

## 7. Source discrepancies

- The CSV750BP data sheet lists 212k for address D4. It should be 121k.
- A "paged module" command table from another Advanced Energy document (with `PAGE`, `VOUT_COMMAND`, `ILIMIT_TRIM`) and the SL Power GU300 note (`WP-2005`, address 0x58, voltage formula `(Hi×100 + Lo)/100`) do not describe this unit.
- The CSV2000BP-family command table lists `READ_IIN`, `READ_VOUT`, `READ_IOUT`, `READ_POUT`, `READ_TEMPERATURE_3`, `STATUS_WORD`, `CAPABILITY` and `PMBUS_REVISION`. They are not served here.

## 8. Open items

1. Confirm 0x6F as truncated integer output amps with a series ammeter at a known load.
2. Ask Advanced Energy support (productsupport.ep@aei.com) for the command list for part 00YL557.
3. Decode 0x70 to 0x72 and 0x13/0x14 if needed.
4. Test PMBus on standby alone, and SMB_ALERT#, PWOK and PRESENT electrically.
5. Repeat on a second unit or firmware version to see whether the command set differs.
