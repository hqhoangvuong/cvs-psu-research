# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository status

Research workspace for PMBus/I2C communication with the Artesyn (Advanced Energy) CSV750BP AC-DC front end. The deliverable, `CSV750BP_PMBus_interface.md`, is written from bench measurements on one unit (MFR_MODEL `00YL557`). Reference PDFs are in `docs/`, extracted facts in `notes/`, ESP8266 sketches in `firmware/`. Update the document and `notes/probe_run1_analysis.md` when new measurements arrive.

Key measured facts (details in the document):
- 7-bit address 0x68, 2.2 kΩ pull-ups required (10 kΩ made the PSU stop answering), every implemented command carries an SMBus PEC, and a reply with no valid PEC means "not implemented" (stale byte then 0xFF).
- READ_VOUT/IOUT/IIN/POUT, STATUS_WORD, CAPABILITY and PMBUS_REVISION are not served, even after PAGE writes, delays and double reads. Do not read the stale bytes as data.
- Served: READ_VIN, READ_PIN, READ_EIN/EOUT (EOUT only tracks input power), temperatures 1 and 2, fan speed, identity strings, individual status bytes, vendor register 0x6F (integer output amps, probably truncated).
- The CSV2000BP command table does not carry over; the notes' source tables are family documents, not this unit's firmware.

## Goal

Produce a PMBus/I2C interface document for the **CSV750BP**. Advanced Energy publishes no CSV750BP PMBus document; its data sheet (7 pp.) has no command list. The CSV2000BP technical reference note is the closest published source, so the CSV750BP document is derived from it. Mark every command or value that is inferred from the CSV2000BP rather than confirmed for the 750BP. The 750BP data sheet itself confirms only:

- the same output connector signals: SDA = S11, SCL = S12, ADDRESS = S15, SMBUS_RESET = S13, SMB_ALERT# = S8, PSON = S16/S17, PWOK = S6, PRESENT = S7;
- the same 8-bit address scheme, D0–DE, set by a resistor on S15;
- "PMBus compliant" as a feature bullet.

Data sheet discrepancy: the 750BP address table lists **212k** for D4 (9.01 V). Both the divider math against the 40.2 kΩ pull-up and the CSV2000BP note give **121k**. Treat 212k as a typo.

Ways to confirm 750BP behaviour: send `QUERY` (0x1A) per command, read `PMBUS_REVISION` (0x98), `MFR_MODEL` (0x9A) and `MFR_REVISION` (0x9B), and read `VOUT_MODE` (0x20) and `CAPABILITY` (0x19). Or ask Advanced Energy technical support (productsupport.ep@aei.com).

## Notes (`notes/`)

`notes/` holds facts extracted from all three PDFs. Read `notes/README.md` first: it indexes the files, separates confirmed-for-750BP from inferred-from-2000BP, and lists inconsistencies in the sources. Use these before re-reading the PDFs. The CSV2000BP command table is in `notes/pmbus_commands_csv2000bp.csv`. Add measured 750BP results alongside it (for example `notes/pmbus_commands_csv750bp_measured.csv`) rather than editing the 2000BP file.

## Firmware (`firmware/`)

- `firmware/pmbus_probe/pmbus_probe.ino`: ESP8266 (Arduino core) probe. Identity/status/telemetry reads, framing variants, a PEC-based command support sweep (the PSU has no working QUERY), `w` watch mode with marker keys, `v` delay/double-read test, and `p` PAGE write test (asks for `y`; the only write it ever does). Everything else is read-only; do not add writes to OPERATION or CLEAR_FAULTS without the user asking. The user has run it on a real unit; its output is recorded in `notes/probe_run1_analysis.md`.
- `firmware/csv750_monitor/csv750_monitor.ino`: read-only PEC-checked monitor (VIN, PIN, EIN average power, temperatures, fan, status bytes, 0x6F as integer amps) with optional INA260/INA226 output metering. Compile-checked only against stub headers here; keep struct definitions above functions (Arduino auto-prototypes).
- `firmware/csv750_pmbus_esp8266.ino`: the user's own earlier monitor sketch; leave it alone.

Bench notes: ESP8266 with 2.2 kΩ pull-ups, ~220 V mains, PSON hardwired asserted (main output on at AC). Loads used: none, and a Wii console (about 20 to 36 W input). Only steady-state readings count as evidence; values captured while unplugging are garbage.

## Reference documents (`docs/`)

- `TRN-AC-DC-CSV2000BP-3-Release-1-1-(2021-05-15).pdf` — CSV2000BP technical reference note. This is the primary spec: electrical specs, control signals, I2C/PMBus addressing, and the full supported PMBus command list (around pp. 28–40).
- `csv750bp_ds_1502310258.pdf` — CSV750BP data sheet (750 W, 90–264 Vac). Sibling product.
- `WP-2005 PM BUS communication setup.pdf` — SL Power GU300 app note. It is a generic PMBus tutorial (write/read transaction sequences, STATUS_WORD decoding) and **not** CSV-specific. Its hardware details do not apply to the CSV parts.

PDF reading: use the `Read` tool with `pages` (max 20 per call); it returns page images, which suit the spec's tables. If it fails with a `pdftoppm` error, Poppler's `bin` folder is missing from the app's PATH (it is installed under the WinGet packages directory), so restart the app after fixing PATH. The PDF Tools MCP server is limited to Documents/Downloads/Desktop, so it can't open files in this repo. `pdftotext -layout <file> <out.txt>` works from Bash for text (write to the scratchpad), but it garbles table layouts.

## Domain facts that are easy to get wrong

Do not apply the GU300 app note's values (address 0x58, 20 kΩ internal pull-ups, J300 pins) to CSV hardware. For the CSV2000BP-3:

- **Addressing**: the address is set by a host-side resistor from the ADDRESS pin (S15) to return, forming a divider with the PSU's internal 40.2 kΩ pull-up to 12Vsb. The datasheet table lists **8-bit** addresses D0–DE in steps of 2 (Open = D0, 5.76k = DE). The 7-bit address is the 8-bit value >> 1 (D0 → 0x68). Confirm the 7-bit/8-bit convention before passing an address to any I2C library.
- **Pull-ups**: the PSU has **no** internal pull-ups. The system must supply about 2.2 kΩ to 3.3 V on SDA (S11) and SCL (S12). The data sheet also recommends 200 pF.
- **Timing**: guaranteed speed is 100 kHz. Leave at least 15 ms between consecutive transactions. The PSU stretches the clock, with a 100 ms maximum time-out.
- **Power state**: PMBus is only reachable when the PSU is powered up. The bus can be powered from the standby output of a parallel unit, so standby outputs must be tied together in N+N or parallel setups.
- **Data formats**: the command table (pp. 34–38) only says "Linear" for READ_* telemetry. It does not say LINEAR11 or LINEAR16. `VOUT_MODE` (0x20) defaults to 0x17, i.e. linear mode with exponent −9, so `READ_VOUT` (0x8B) is most likely LINEAR16 (volts = raw × 2⁻⁹). The other READ_* commands (VIN, IIN, IOUT, POUT, PIN, temperatures, fan speed) are presumably LINEAR11 (5-bit exponent, 11-bit mantissa). Check against real hardware. Do not copy the GU300 note's `(Hi*100+Lo)/100` voltage formula. READ_EIN/EOUT (0x86/0x87) are 6-byte block reads with no format given.
- **Control**: `OPERATION` (0x01) is 0x80 = on, 0x40 = immediate off. The default `ON_OFF_CONFIG` (0x1C) also requires the CONTROL pin to be asserted, active-low, so a serial "on" alone will not energise the output. `CAPABILITY` (0x19) defaults to 0x90, which means PEC supported, 100 kHz max and no SMBALERT pin. `STATUS_WORD` is 0x79 and `CLEAR_FAULTS` is 0x03.
- **Model**: CSV2000BP-3 outputs 12.2 Vdc main (1–163.9 A) plus a 12 V/3 A standby. Input is 180–264 Vac.
