# Probe run 1 (pmbus_probe.ino) - CSV750BP, address 0x68

Bench: ESP8266, 10 kOhm pull-ups, ~220 V mains. Main output state not recorded. OPERATION=0x00 and READ_PIN=2 W suggest main off, standby only.

## Confirmed
- Answers at 7-bit 0x68 (8-bit 0xD0), the open-ADDRESS default.
- Identity blocks read cleanly: MFR_ID "EMER", MFR_MODEL "00YL557", MFR_REVISION "00YL556", MFR_LOCATION " K1181", MFR_SERIAL "67M2P9". The first block byte is the length.
- READ_VIN is LINEAR11 (227.25 V). MFR_POUT_MAX is 750 (LINEAR11).
- ON_OFF_CONFIG is 0x15, not the 2000BP default 0x1C: OPERATION is ignored (cmd bit 0); CONTROL pin (active low, immediate) governs.
- VOUT_MODE is 0x1B (linear, exp -5), not 0x17 as on the 2000BP. If READ_VOUT is LINEAR16, 1 LSB = 1/32 V.
- QUERY (0x1A) is not usable as framed (failed). The PSU ACKs all 256 command codes, so the ACK fallback map is meaningless.

## Open: word reads with high byte 0xFF
READ_IIN, IOUT, POUT, VOUT, TEMP_3, MFR_IOUT_MAX, MFR_MAX_TEMP_1..3, STATUS_WORD all return a high byte of 0xFF. The values are identical to the earlier session, so they are deterministic, not noise. MFR_IOUT_MAX and MFR_MAX_TEMP_* are static, so a bus glitch alone does not explain them.
Other words (VIN, T1, T2, FAN, PIN, POUT_MAX) are fine, so the master mostly works.
PMBUS_REVISION 0x5F, CAPABILITY 0xFC and STATUS_BYTE/STATUS_WORD disagreement also look wrong.

Hypotheses, in order of how cheaply they can be tested:
1. The low byte is the real value and the PSU returns only 1 data byte for those commands (second byte is the floating bus). Test: read each with requestFrom(n=1), then n=3; check whether PEC or extra bytes appear.
2. Master framing: the PSU wants STOP between the command write and the read (endTransmission(true)) rather than a repeated start. Test: both variants on IIN, IOUT and VIN.
3. Marginal bus: 10 kOhm gives about 1.7 us rise time at ~200 pF, over the 1 us limit. Test with 2.2 kOhm to 3.3 V or Wire.setClock(50000).
4. Main output off: some commands may return placeholders until PSON asserts. Test: repeat with the main output on (CONTROL asserted) at no load, then a known load.

## Next capture requested
Same probe with: (a) main output on, no load; (b) known load; for each, note the state. Plus the hypothesis 1 and 2 variants above (to be added to the probe).

## Run 2 (variants block) findings
- **PEC is always appended.** READ_VIN with n=3 and n=4 returns `8C F3 35 FF`: the third byte is the SMBus PEC (CRC-8, poly 0x07, over D0 88 D1 8C F3 = 0x35; verified offline, and the n=3 repeated-start read `8D F3 20` also matches 0x20). The fourth byte is the floating bus (0xFF). So the 750BP supports PEC on reads, and any read longer than the data returns PEC next. This fits CAPABILITY bit7. It does not explain the 0xFF high bytes.
- READ_VIN is stable (0xF38C/0xF38D, 227 V). Stop-before-read gives the same data, so STOP versus repeated start is not the issue for the commands that work.
- **The PSU stops responding mid-run.** From the 8th variant read (second repeat of READ_VIN, n=2) every endTransmission fails, for every command, to the end of the run. This is not one command misbehaving: the slave stopped ACKing its address (or SDA/SCL is stuck). The probe did not print the error code, so NACK versus bus-stuck/timeout is unknown. The earlier ~40 transactions worked. The variants for IIN, IOUT, etc. never ran, so hypotheses 1 and 2 are still untested.
- READ_TEMPERATURE_1 moved 22 to 25 degC, fan 6352 to 5984 rpm, PIN 2 to 4 W (0xCA00): live values, and the 0xFF-high-byte words are unchanged between runs (deterministic).
- Probe updated: prints the endTransmission error code, SDA/SCL levels at a fault, waits 200 ms, clocks out the bus (9 SCL pulses + STOP), retries up to 3 times, and checks `pec_last` on each variant read.
