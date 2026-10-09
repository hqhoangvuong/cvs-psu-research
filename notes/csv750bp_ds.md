# CSV750BP-3 data sheet: extracted facts

Source: `docs/csv750bp_ds_1502310258.pdf` (7 pp., ENG-CSV750BP-235-01 12.09, ©2020). It has **no PMBus command list, no I2C timing, no pull-up guidance.** For those, see `csv2000bp_trn.md` (same family; inferred, not confirmed for 750BP).

## Product
- 750 W 1U, hot-pluggable, N+N redundant, active current sharing, 80 PLUS Platinum, PMBus compliant, two-year warranty. Model CSV750BP-3, part number **700-013700-1000**. Airflow standard (forward). (p.3)
- Input 90–264 Vac, 47–63 Hz, max 8.6 A at 100 Vac, inrush 30 Apk, PF > 0.9 from 20 % load, hold-up 12 ms at full load, leakage 0.8 mA. (p.2)

## Outputs (p.2)
| | Main | Standby |
|---|---|---|
| Nominal | 12.2 V (−0.20 %/+0.20 %) | 12.0 V (−3.5 %/+3.5 %) |
| Total regulation | 11.59–12.81 V | 11.4–12.6 V |
| Ripple | 120 mVp-p | 120 mVp-p |
| Current | 1.0 A (transient-test minimum) to 61.5 A | 0–2.5 A |
| Load capacitance | 1,000–20,000 µF | 50–500 µF |
| Start-up from AC | ≤ 3000 ms | ≤ 2500 ms |
| Rise time | 2–20 ms | 2–20 ms |

Current sharing within ±10 % starting at 30 % load.

## Protection (p.2)
- Main OCP: >100 % to 125 %, latch. OVP 13.8 V, latch. UVP 10.0 V max. OTP and fan-fault protection: yes. THROTTLE warning of at least 1 s before latching off.
- Standby: OCP 3.9 A, OVP 13.8 V, UVP 10.0 V max. **Auto-recovery.**

## LED indicators (p.3)
| State | Input good (green) | Output good (green) | Fault (yellow) |
|---|---|---|---|
| Output on and OK | On | On | Off |
| Standby (input present, main off, or zero-output mode) | On | Blinking 1 Hz | Off |
| No input / input out of range | Off | Off | Off |
| OCP, over-subscription, OVP, fan failure or OTP | On | Off | On |

## Environmental (p.3)
Operating 5–50 °C (derate 1 °C per 600 ft above 3000 ft), up to 10,000 ft, 8–93 % RH non-condensing. Non-operating −40 to +60 °C (note the 2000BP note says +70 °C), up to 50,000 ft. MTBF 500 kh at 40 °C, 70 % load. Life at least 5 years. RoHS. Safety: UL/cUL, CB, CE, KC, CCC/CQC.

## Timing (p.4; ms)
| Label | Meaning | Min | Max |
|---|---|---|---|
| Tsb_On | AC to standby in regulation | | 2500 |
| TVout_rise | 10 %→90 % | 1 | 50 |
| TAC_On_Delay | AC to main in regulation | | 3000 |
| TPWOK_On | Regulation to PWOK | 180 | 220 |
| TACOK_PWOK_Delay | ACOK low to PWOK deassert | 6 | |
| TVout_Hold-up | Loss of AC to main out of regulation | 12 | |
| Tsb_Hold-up | Loss of AC to standby out of regulation | 50 | 1000 |
| TPWOK_Off | PWOK deassert to output out of regulation | 2 | |
| TPSON_PWOK | PSON deassert to PWOK deassert | | 1 |
| TPSON_On_Delay | PSON assert to output in regulation | | 100 |

Same values as the 2000BP (T1–T10). The 750BP diagram also shows EPOW and TACOK_off_dwell.

## Mechanical and connector (pp.5–6)
- 80.0 × 40 × 195 mm body (overall length 206.0 mm with handle, 191.0 ±0.4 mm to the face). Card-edge output, same mating connector as the 2000BP: FCI Amphenol HPCE 10122238-320424FLF.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| S1, S2 | Reserved | S13 | SMBUS_RESET |
| S3 | +Vsense | S14 | Reserved |
| **S4** | **PSKILL** | S15 | ADDRESS |
| S5 | Reserved | S16, S17 | PSON |
| S6 | PWOK | S18 | ACOK |
| S7 | PRESENT | S19 | Reserved |
| **S8** | **SMB_ALERT#** | S20 | THROTTLE |
| S9 | ISHARE | S21 | Reserved |
| S10 | RETURN | S22 | −Vsense |
| S11 | SDA | S23, S24 | Reserved |
| S12 | SCL | | |

Power blades: P1–P8, P29–P36 Vo; P9–P18, P21–P28 RTN; P19–P20 VSB.

## Addressing (p.6): "pull-down at system side, 1 % or better"
| Resistor | Voltage | 8-bit addr |
|---|---|---|
| Open | 12.00 V | D0 |
| 280 k | 10.49 | D2 |
| **212 k** | 9.01 | D4 |
| 68.1 k | 7.55 | D6 |
| 40.2 k | 6.00 | D8 |
| 23.7 k | 4.45 | DA |
| 13.3 k | 2.98 | DC |
| 5.76 k | 1.50 | DE |

"212 k" for D4 is almost certainly a typo for **121 k** (2000BP note and divider math with 40.2 k to 12 V both give 121 k).

## Differences vs the CSV2000BP note
- **S4 is PSKILL** on the 750BP (short pin that kills the PSU on removal). The 2000BP note lists S4 as reserved (GND at system).
- Pin names differ slightly: SMB_ALERT#, ACOK, PSON.
- Standby is 2.5 A max vs 3 A. Main current 61.5 A vs 163.9 A. OCP thresholds differ (750BP: 100–125 % of rating).
- Data sheet gives non-operating temperature −40 to +60 °C vs +70 °C.
