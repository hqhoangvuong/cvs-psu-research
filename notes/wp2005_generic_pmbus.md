# WP-2005 "PM BUS communication setup": extracted facts

Source: `docs/WP-2005 PM BUS communication setup.pdf` (21 pp.). It is an SL Power Electronics app note for the **GU300** family, not for Artesyn CSV. The product-specific values (address 0x58, J300 pins, 20 kΩ internal pull-ups, 25 ms window, `(Hi*100+Lo)/100` voltage formula) **do not apply to CSV parts**. The transaction patterns and the SMBus/PMBus basics below are generic and do apply.

## Generic PMBus facts
- PMBus is SMBus on an I2C physical layer. Devices are slaves only. The master sends and receives. Each slave has a unique 7-bit address.
- Not every PMBus command is supported by every PSU. Full list: PMBus Spec Part II (pmbus.org).
- Keep cables short (the note recommends < 50 cm). 100 kHz recommended. 3.3 V pull-ups.

## Transaction patterns (7-bit addr, write byte = addr<<1, read byte = addr<<1 | 1)
**Write byte/word**: `S | addr+W | ACK | cmd | ACK | data... | ACK | P`
- Turn off (GU300): cmd 0x01, data 0x00. Turn on: cmd 0x01, data 0x80.
- Clear faults: cmd 0x03 only, no data (send byte).

**Read byte/word**: `S | addr+W | ACK | cmd | ACK | Sr | addr+R | ACK | [slave stretches clock] | data lo | ACK | data hi | NACK | P`
- Master ACKs every byte except the last, then NACKs and sends STOP.
- 16-bit values arrive **low byte first**.

## Worked examples (GU300 values, for the pattern only)
| Read | Bytes returned | Meaning |
|---|---|---|
| STATUS_WORD (0x79), unit on, no faults | 00 00 | clean |
| STATUS_WORD, unit off | lo 0x40, hi 0x08 | OFF (b6), POWER_GOOD# (b11) |
| STATUS_WORD, fault | lo 0x51, hi 0x48 | OFF, IOUT_OC (b4), NONE_OF_ABOVE (b0); b14 IOUT, b11 POWER_GOOD# |
| READ_VOUT (0x8B) | lo 0x51, hi 0x09 | GU300 formula gives 23.85 V; **CSV uses LINEAR, not this formula** |
| OPERATION (0x01) read | 0x80 | last serial command sent |

## OPERATION (0x01) secondary values on GU300
- 0x00 = instant shutdown. 0x40 = graceful shutdown with ~224 ms delay. 0x80 = turn on if conditions in ON_OFF_CONFIG are met. Anything else sets CML invalid-data flag.
- Reading 0x01 returns the last serial control command.

## ON_OFF_CONFIG (0x02) on GU300 (Appendix, Table A1)
Bits: b4 controllable, b3 serial enable, b2 control-switch enable, b1 control-switch polarity, b0 no control delay. Highlights:
- 0x00–0x0F: uncontrollable, always on. 0x10–0x13: invalid.
- 0x14–0x17: control by switch only (serial disabled).
- 0x18–0x1B: serial only, switch disabled.
- **0x1C (default)**: serial + switch both needed. Turn-on command present by default, switch low = on. Off by serial command or switch high/floating with delay SD1.
- 0x1D: as 0x1C but shutdown delayed by SD0. 0x1E/0x1F: as 0x1C/0x1D with switch polarity inverted.
- SD0 = instant shutdown. SD1 = ~224 ms delayed shutdown if no fault or low-severity fault.

The CSV2000BP uses the same default 0x1C (with bits b4 = 1, b3 = 1, b2 = 1, b1 = 0 active low, b0 = 0). Its documentation describes the bits similarly (CSV2000BP note p.34).
