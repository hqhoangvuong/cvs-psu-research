# notes/: digested reference material

Extracted from the PDFs in `../docs/` so they can be searched without re-reading the PDFs. All page references are printed page numbers in the source.

| File | Contents |
|---|---|
| `csv750bp_ds.md` | CSV750BP data sheet: specs, timing, connector pinout, address table, differences vs the 2000BP |
| `csv2000bp_trn.md` | CSV2000BP technical reference note: specs, protection, signals, I2C rules, addressing, PMBus quirks and inconsistencies |
| `pmbus_commands_csv2000bp.csv` | The CSV2000BP supported PMBus command table (code, name, default, access, bytes, format, description) |
| `wp2005_generic_pmbus.md` | SL Power GU300 app note: generic PMBus transaction patterns and ON_OFF_CONFIG semantics (**not CSV-specific**) |

## Confidence key for the CSV750BP PMBus document
- **Confirmed for 750BP** (from its data sheet): connector pinout (SDA S11, SCL S12, SMB_ALERT# S8, SMBUS_RESET S13, ADDRESS S15), 8-bit address scheme D0–DE, "PMBus compliant", electrical and timing specs.
- **Inferred from the CSV2000BP** (same family, nothing from the 750BP): the command list, defaults, bit maps, I2C timing rules (100 kHz, 15 ms gap, 100 ms stretch, 2.2 kΩ pull-ups), data formats.
- **Unknown, needs hardware or vendor**: which commands the 750BP actually supports, its data formats and scaling, PEC support, FRU EEPROM address, whether I2C answers on standby alone.

## Known inconsistencies in the sources
- 750BP data sheet lists 212k for address D4. The 2000BP note and the divider math give 121k.
- 750BP S4 = PSKILL. 2000BP S4 = reserved (GND at system).
- 2000BP: CAPABILITY says SMBALERT unsupported but the S8 pin exists. OPERATION "01 = immediate off" differs from standard PMBus. OCP limit 95.9 A looks wrong. Address pull-up shown as 3.3 V in one figure and 12 V elsewhere.
- Min/max details for each are listed in `csv2000bp_trn.md` under "PMBus quirks".
