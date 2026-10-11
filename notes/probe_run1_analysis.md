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

## Run 5: watch mode (no load, output on, ~60 s, 1 row/s)
Fields are data bytes in receive order (low byte first), PEC dropped.
- **Static** (never changed): OPERATION 00, ON_OFF_CONFIG 15, VOUT_MODE 1B, 0x30 block `01 00 00 00 00`, 0x3A D0, 0x3B `E80B` (0x0BE8), 0x6F `0000`, STATUS_VOUT/IOUT/INPUT/TEMP 00, FANS 00, 0x83 0C, 0x84 00, MFR_ID, 0xA7/A8/B0 `EE02` (750), 0xB9 01, 0xBA `5100` (81), 0xBB/BC `8403` (900), 0xBD `2B1D` (0x1D2B), 0xBE/D9/DA 00, 0xDE `7A01`, 0xDF FB, 0xE3 `0101`, 0xFA..FE `30 35 30 3B 03`.
- **Live and decodable:** READ_VIN 0x88 (225.75 to 227.0 V in 0.25 V steps, LINEAR11); READ_TEMPERATURE_1/2 (27, 31 degC); READ_FAN_SPEED_1 0x90 (about 6000 rpm); READ_PIN 0x97.
- **PIN mirrors:** 0x6C is a constant `80CA` (= 5.0 W LINEAR11, a snapshot); 0xE1 alternates `80CA`/`00CB`/`80CB` (5.0, 6.0, 7.0 W LINEAR11, exp -7); 0x6E is the integer watts `0500/0600/0700`. So the no-load input power is 5 to 7 W, and 0x6E is PIN as an integer. 0x6F stays at 0 and is probably a similar integer output reading (POUT or IOUT) at 0 W / 0 A, but that cannot be told apart without a load.
- **Flickering, undecoded:** 0x70 first byte `60`/`72` (rest `01 FF 02`); 0x71 second byte `00`/`01` (rest `00 12 00`); 0x72 `FB`/`FD`, `D2`/`FE`, 9E (4-byte block). These change in the low byte only, like ADC raw counts, so they are probably raw sensor values.
- STATUS_CML read 0x82 at the start of the watch and 0x00 afterwards. Bit 7 = invalid/unsupported command, bit 1 = other communication fault: those bits are set by our reads of unsupported command codes (READ_IOUT etc.) and cleared by the status read. This is independent evidence that the PSU itself treats those commands as invalid.
- No candidate for VOUT (12.3 V) or IOUT stands out at no load. The output voltage is not obviously exposed.

## Run 6: Wii as load (watch mode, marks 1-5, about 185 s)
Timeline from the marks and PIN: 1 = Wii unplugged (to ~79 s); 2 = Wii plugged in, console off (79 to ~88 s); the Wii was switched on at ~88 s (PIN rose before mark 3 at 95 s); 3 and 4 = Wii on at the menu (no game was available, so mark 4 is the same state); the Wii was switched off at ~151 s; 5 = Wii off (standby) to the end. READ_PIN: 5-7 W unplugged, 19-23 W on, about 14 W after switching off (the Wii draws about 8 W in standby).

New PEC-valid codes from the longer sweep (56 total): 0x13 (block 14), 0x14 (block 21), 0x86 and 0x87 (block 6 each), 0xD5 (block 7, static `07 15 10 00 00 00 00`), and the long ASCII blocks 0x9A, 0x9B, 0x9C, 0x9E.

**READ_EIN (0x86) and READ_EOUT (0x87) are implemented.** Block of 6: accumulator (2 bytes little endian, wraps at about 32768), rollover count (1 byte), sample count (3 bytes little endian). Mean value per sample = delta(rollover x 32768 + accumulator) / delta(samples), checked across two rollovers.
- EIN: about 14 samples per second. Mean per sample: 5.1 (unplugged), 20.8 (Wii on), 14.3 (Wii off). This matches READ_PIN in watts, so EIN is input power in watts at finer resolution than READ_PIN.
- EOUT: about 310 samples per second. Mean per sample: 8.0 exactly (unplugged and Wii off-line), about 27-30 (Wii on), about 17.7 (standby). It scales with load, but the unit is **not** watts: 8.0 at no load is above the 5 W input power, and 28 is above the 21 W input power. It is not proportional to PIN either (ratios 1.57, 1.35, 1.24). Treat as an unscaled load-dependent value.
- 0x13 and 0x14 are extended snapshots containing PIN as integer words, copies of the EIN counters, and several slowly changing words (0x13 data: PIN int, PIN int, `6A 02`, `18 04`, then the EIN accumulator and count). Unscaled and unidentified.

**Candidates that respond to load:**
- **0x6F** (word): 0 unplugged, 1 or 2 with the Wii on (alternating), 0 with the Wii off. Consistent with output current in whole amps at about 1.5 A (about 18.5 W out for about 21 W in). Not confirmed: after switching the Wii off, PIN stayed at 14 W but 0x6F read 0, which suggests it either truncates, or covers only the main rail. Needs a series multimeter reading.
- **0x6E**: integer watts of input power (matches PIN, 5-7 unplugged, 19-23 on, 14 standby).
- **0x70** (block 4, two words): 352 / 767 unplugged, about 540-560 / 960 standby, 722-741 / 1154-1171 on. Linear in PIN at about 25 counts per watt. Raw input-side ADC values, not output.
- **0x71** (block 4, second word): 18 unplugged, 27-29 standby, 31-45 on. Roughly 1.5 x PIN + 9. Unidentified.
- **0x72**, 0x83, 0x84, 0xB9-0xBE, 0xD9, 0xDA, 0xDE, 0xDF, 0xE3, 0xFA-0xFE: unchanged by the load.
- 0x13 word 4 (`18 04` = 1048 up to 1050) rose by 1-2 counts and word 3 (`6A 02` and similar, 618-627) is noisy. Neither is shown to be output voltage; 1048/85 and 620/50 both happen to land near the measured 12.33 V, which is probably coincidence.

**Not found:** no register that reports 12.33 V directly, and no output current or power in known units. VOUT and POUT remain unidentified.
