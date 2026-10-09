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

## Run 3 (2.2 kOhm pull-ups, main output still off)
- **The bus failure is gone.** With 2.2 kOhm pull-ups the full variants block completed, with no write errors. The 10 kOhm pull-ups were the cause.
- **PEC confirmed twice more:** READ_VIN `84 F3 9D` and READ_PIN `00 C2 5D` both end in a correct CRC-8 (verified offline). Two-byte value + PEC + 0xFF filler.
- **The 0xFF-high-byte commands are single-byte replies with no PEC.** READ_IIN, READ_VOUT, READ_IOUT, READ_TEMPERATURE_3, READ_POUT, MFR_IOUT_MAX, MFR_MAX_TEMP_1, STATUS_WORD, STATUS_BYTE return one byte, then 0xFF 0xFF 0xFF for any longer read (e.g. IIN n=4: `1D FF FF FF`). No PEC follows, and the byte is the same every time (0x1D, 0x37, 0x5C, 0x63, 0x89, 0x70, 0xFB, 0x09, 0x1C). Framing (repeated start vs STOP) makes no difference. So the "0xFF high byte" was the bus floating after a 1-byte reply, not a format problem.
- So the earlier "negative" values (IIN -113.5 A and so on) are an artefact of decoding a 1-byte reply as a word. The PSU is not returning an IIN/IOUT/POUT measurement here.
- **Leading hypothesis (untested):** with the main output off, the standby-side controller answers only what it knows (VIN, PIN, temperatures 1 and 2, fan, MFR_POUT_MAX, identity). Everything fed by the main-side controller returns a stale single byte with no PEC. This fits IOUT, VOUT, POUT and TEMPERATURE_3. It does not obviously fit IIN, so it may be wrong.
- **Consequence for earlier data:** the other one-byte reads (PMBUS_REVISION 0x5F, CAPABILITY 0xFC, VOUT_MODE 0x1B, ON_OFF_CONFIG 0x15, OPERATION 0x00, the status bytes, FW revisions) may be the same kind of stale single byte and should not be treated as measured values until re-read with the main output on. They need re-checking for the same no-PEC behaviour.
- Test next: assert PSON (S16/S17, active low, below 0.8 V enables main per the CSV2000BP note; polarity on the 750BP not confirmed) and rerun the probe, at no load and then at a known load.

## Correction (user)
The PSU is hardwired to turn on when AC is applied (PSON always asserted), so the main output was ON in runs 1-3, at no load. The "main output off" reading above was wrong: `OPERATION=0x00` is just ignored (ON_OFF_CONFIG=0x15, cmd bit 0). With the main on and no load, IOUT and POUT should read about 0 and VOUT about 12 V, so the single-byte stale replies are not explained by the output being off. The leading hypothesis is dead; the replies look like commands this firmware does not serve (in the PMBus sense, unsupported), including STATUS_WORD, which disagrees with STATUS_BYTE. Added `pecSweep()` to the probe to map which command codes return data with a valid PEC.

## Run 4: output ON (12.32 V measured, two solid green LEDs, no load); PEC sweep
- The 2000BP command table does not carry over. With the main output on, these standard commands give no valid PEC: CAPABILITY (0x19), PMBUS_REVISION (0x98), STATUS_BYTE (0x78), STATUS_WORD (0x79), READ_IIN (0x89), READ_VOUT (0x8B), READ_IOUT (0x8C), READ_TEMPERATURE_3 (0x8F), READ_POUT (0x96), MFR_IOUT_MAX (0xA6), MFR_MAX_TEMP_1..3 (0xC0..C2). Treat the 0x5F and 0xFC readings as stale bytes, not values.
- Served with valid PEC: OPERATION (00), ON_OFF_CONFIG (15), VOUT_MODE (1B), STATUS_VOUT/IOUT/INPUT/TEMPERATURE (00), STATUS_CML (82, changed from 80 between runs), STATUS_MFR_SPECIFIC, STATUS_FANS_1_2, READ_VIN, READ_TEMPERATURE_1/2, READ_FAN_SPEED_1, READ_PIN, MFR_POUT_MAX, MFR_ID, FW revisions (0xFB, 0xFC), COEFFICIENTS (0x30, block 01 00 00 00 00), and about 25 vendor codes (0x3A, 0x3B, 0x6C, 0x6E, 0x6F, 0x70-0x72, 0x83, 0x84, 0xA8, 0xB0, 0xB9-0xBE, 0xD9, 0xDA, 0xDE, 0xDF, 0xE1, 0xE3, 0xFA, 0xFD, 0xFE).
- Output current/voltage/power must therefore be somewhere else (vendor codes) or not exposed. Unknown until a load is applied and the `watch` mode shows which fields move.
- Limitation: the sweep reads 8 bytes, so blocks longer than 6 data bytes (MFR_MODEL, MFR_REVISION, MFR_LOCATION, MFR_SERIAL) are not matched.
- Raw sweep: 01=00 02=15 20=1B 30=050100000000 3A=D0 3B=E80B 6C=80CA 6E=0500 6F=0000 70=046001FF0226 71=040000120063 72=04FBD29E9AF0 7A=00 7B=00 7C=00 7D=00 7E=82 80=00 81=00 83=0CF0..(0C) 84=00 88=87F3 8D=1B00 8E=1F00 90=EF1A 97=80CA 99=04454D4552 A7=EE02 A8=EE02 B0=EE02 B9=01 BA=5100 BB=8403 BC=8403 BD=2B1D BE=00 D9=00 DA=00 DE=7A01 DF=FB E1=00CB E3=0101 FA=30 FB=35 FC=30 FD=3B FE=03 (data bytes, PEC dropped).
