# CSV2000BP-3 technical reference note: extracted facts

Source: `docs/TRN-AC-DC-CSV2000BP-3-Release-1-1-(2021-05-15).pdf` (41 pp., Rev. 03.02.21_#1.1). Page numbers (p.N) are printed page numbers. PMBus commands are in `pmbus_commands_csv2000bp.csv`.

## Product
- 2000 W 1U hot-pluggable front end, N+N redundant, active current sharing, 80 PLUS Platinum, PMBus compliant. Ordering name CSV2000BP-3.
- Main 12.2 Vdc, 1 A (transient-test minimum; works at zero load) to 163.9 A. Standby 12 Vdc, 0.5–3 A. Airflow: DC connector to handle (normal). Weight 1060 g. (pp.1–2, 15)
- Input 180–264 Vac (198–264 for continuous at full spec), 47–63 Hz. Max input current 11 A at 180 Vac, 10 A at 198 Vac. Inrush 30 Apk. Input fuse 16 A internal, non-serviceable. PF ≥ 0.90 above 10 % load. No-load input ≤ 5 W. Leakage ≤ 0.575 mA. Efficiency at 230 Vac: 89 % at 10 %, 90 % at 20 %, 94 % at 50 %, 91 % at 100 % load. (pp.3–4, 9)
- Isolation: input to output 4243 Vdc, input to safety ground 3000 Vdc. Operating 5–50 °C (derate 1 °C per 600 ft above 3000 ft), up to 10,000 ft. Storage −40 to 70 °C. MTBF 500 kh at 40 °C, 70 % load. (p.3)

## Outputs (Table 3, p.5)
| | Main Vo | Standby Vsb |
|---|---|---|
| Set point | 12.2 V ±0.2 % | 12.0 V ±3.5 % |
| Regulation range | 11.59–12.81 V | 11.4–12.6 V |
| Ripple pk-pk (20 MHz) | 120 mV | 120 mV |
| Load capacitance | 100–25,000 µF | 50–500 µF |
| Current | 1–163.9 A | 0.5–3 A |

Current share accuracy: 10 % at 30–50 % load, 6 % at 50–100 %. Supports 3+1. Sharing not required below 30 % load. Ishare: 5.0 V typ at 100 %, 2.5 at 50 %, 1.25 at 25 %. (p.39)

## Timing (Table 4, p.6; units ms)
| Label | Meaning | Min | Max |
|---|---|---|---|
| T1 | AC applied to Vsb in regulation | – | 2500 |
| T2 | Vo rise 10 %→90 % | 1 | 50 |
| T3 | AC applied to main in regulation | – | 3000 |
| T4 | Vo in regulation to PWOK assert | 180 | 220 |
| T5 | ACOK low to PWOK deassert | 6 | – |
| T6 | Loss of AC to Vo out of regulation (hold-up) | 12 | – |
| T7 | Loss of AC to Vsb out of regulation | 50 | 1000 |
| T8 | PWOK deassert to Vo out of regulation | 2 | – |
| T9 | PSON deassert to PWOK deassert | – | 1 |
| T10 | PSON assert to Vo in regulation | – | 100 |

## Protection (p.10)
- OVP: main and standby ≥ 13.8 V. **Latches off**, cleared by recycling PSON or input.
- UVP: main shuts down below 10.0 V. Standby UVP also 10.0 V.
- OTP: at the limit all outputs except standby latch off. **OT_WARN** (STATUS_TEMPERATURE) is set first. If still present after **30 s**, THERMAL_FAULT (STATUS_MFR_SPECIFIC) and OT_FAULT are set and the PSU shuts down.
- OCP main: limit 95.9 A, latch. Shuts down only if over-current lasts more than 50 ms.
- OCP standby: 3.9 A, auto-recovers. Does not shut down if over-current lasts less than 1 ms. An OCP fault on main does not take down standby.

## Output connector: card edge
Mating connector: FCI Amphenol HPCE 10122238-320424FLF. AC inlet IEC320-C14 (L = pin 1, N = pin 2, earth = pin 3).

Power blades: P1–P8 and P29–P36 Vo, P9–P18 and P21–P28 return, P19–P20 Vsb.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| S1, S2 | Reserved | S13 | SMBUS_RESET |
| S3 | +MAIN_VRS / +Vsense | S14 | Reserved |
| S4 | Reserved (GND at system) | S15 | ADDRESS |
| S5 | Reserved | S16, S17 | PSON_L |
| S6 | DC_GOOD / PWOK | S18 | EPOW / ACOK |
| S7 | PRESENT | S19 | Reserved |
| S8 | SMBALERT | S20 | THROTTLE |
| S9 | ISHARE | S21 | Reserved |
| S10 | GND / RETURN | S22 | −MAIN_VRS / −Vsense |
| S11 | SDA | S23, S24 | Reserved |
| S12 | SCL | | |

Signal behaviour (pp.23–27):
- **PWOK (S6)**: active-high, driven by PSU. Host supplies pull-up to 3.3 V or 5 V (5 k and 10 nF drawn). Low = fault, main latched off.
- **PRESENT (S7)**: active-low, grounded inside PSU. Host pull-up.
- **SMBALERT (S8)**: active-low open collector. Host pull-up to 3.3 V or 5 V. Only asserts while main output is enabled. Asserts on EPOW/ACOK, OT warning, overload, any unmasked status bit. Clears by clearing the STATUS bits, recycling PSON or input, or masking with SMBALERT_MASK. Supports Intel Node Manager behaviour. CLEAR_FAULTS returns it to default.
- **SMBUS_RESET (S13)**: active-low input resets all SMBus interfaces. Host may drive low 1 µs to 500 ms. PSU has a pull-up. Host should use an open-collector driver.
- **PSON_L (S16, S17)**: active-low. Below 0.8 V with EPOW high enables main. Standby unaffected.
- **EPOW/ACOK (S18)**: active-low open collector, normally high (above 2.0 V) when AC is in range. Host pull-up to 12 Vsb or the system bus.
- **THROTTLE (S20)**: active-low open collector, no PSU pull-up. Asserts above 172 A (deasserts below 156 A, 1 ms delay each) or on OT warning. Shuts down if over-subscription lasts more than 5 s. Absolute max 213 A for 5 s, shutdown if load does not drop below 70.1 A.
- **Remote sense (S3, S22)**: compensates up to 500 mV of path drop and will not raise Vo to OVP.

LED indicators (p.14): AC green = EPOW/ACOK de-asserted (input good). DC green = PWOK asserted. Yellow = fault.

## I2C / PMBus (pp.28–32)
- Bus carries **PMBus and FRU data** (an EEPROM shares SDA/SCL, p.29). The EEPROM address is not stated.
- Bus is powered from the PSU's internal 3.3 V or from the standby output of another paralleled PSU. In parallel or redundant systems **tie the standby outputs together**. PMBus works only when the PSU is powered up.
- Guaranteed speed **100 kHz** (standard mode: 10–100 kHz). Measured 90.9 kHz. CAPABILITY default says 100 kHz max.
- **No internal pull-ups.** Recommended 2.2 kΩ to 3.3 V per line plus 200 pF. Series 20 Ω and 47 pF on the PSU side (p.29 figure).
- Logic: high 3.3 V nominal (spec 2.1–5.5 V), low 500 mV nominal (spec max 2000 mV) (p.30).
- Minimum **15 ms** between consecutive I2C communications.
- Clock stretching is used. Maximum stretch timeout **100 ms**.
- Bus noise from PSU < 300 mV pk-pk.
- Address: 8-bit, from the ADDRESS pin (S15) with a host resistor to return. The PSU has 40.2 kΩ ±1 % to 12 Vsb (text says 12Vaux on p.28), and the figure on p.29 labels the pull-up as "System 3.3V", which conflicts with the 12.00 V open-pin voltage in Table 8 (treat 12 V as right). **The PSU must reply to commands sent with the 8-bit address.**

| Host resistor | Pin voltage | 8-bit addr | 7-bit |
|---|---|---|---|
| Open | 12.00 | 0xD0 | 0x68 |
| 280 k | 10.49 | 0xD2 | 0x69 |
| 121 k | 9.01 | 0xD4 | 0x6A |
| 68.1 k | 7.55 | 0xD6 | 0x6B |
| 40.2 k | 6.00 | 0xD8 | 0x6C |
| 23.7 k | 4.45 | 0xDA | 0x6D |
| 13.3 k | 2.98 | 0xDC | 0x6E |
| 5.76 k | 1.50 | 0xDE | 0x6F |

Timing measured with Artesyn 73-769-001 USB-to-I2C adapter and Universal PMBus GUI (p.30): tHD;STA 4.74 µs, tLOW 4.86 µs, tHIGH 4.84 µs, rise 670–710 ns, fall 146–157 ns, tBUF 95 µs.

## PMBus quirks in the note (pp.34–38)
- Data formats are only labelled "Linear", "Hex", "Bitmapped". LINEAR11 vs LINEAR16 is not stated. `VOUT_MODE` = 0x17 (exponent −9) implies LINEAR16 for output voltage.
- OPERATION: b7:6 = 10 is ON and 01 is "immediate turn OFF". In standard PMBus, 0x00 is immediate off and 0x40 is soft off. Check on hardware.
- ON_OFF_CONFIG default 0x1C: serial control needs the CONTROL pin (PSON) asserted as well.
- CAPABILITY default 0x90 reports SMBALERT pin as not supported (b5 = 0), but the pin S8 exists. Inconsistent.
- STATUS_WORD table (p.36) lists high-byte bits 15 VOUT, 14 IOUT, 13 INPUT, 11 POWER_GOOD#, 10 FANS (b12 MFR omitted), and low-byte bits 6, 4, 3, 2, 1, 0 (b7 BUSY and b5 VOUT_OV missing from the word table, present in STATUS_BYTE).
- STATUS_VOUT lists only b7 (OV fault) and b4 (UV fault). STATUS_INPUT b0 is labelled POUT_OP_WARNING, likely a typo.
- The OCP limit of 95.9 A (p.10) is below the 163.9 A rated maximum and the 172 A THROTTLE threshold. This looks like a typo, so do not rely on it.
- Command list has gaps (nothing between 0x51 and 0x78, no VOUT/VIN limit commands, no READ_EXT). Use QUERY to confirm.
