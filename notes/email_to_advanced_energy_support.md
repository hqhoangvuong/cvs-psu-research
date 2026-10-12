To: productsupport.ep@aei.com
Subject: CSV750BP-3 PMBus command list and register map request (MFR_MODEL 00YL557)

Hello,

I am reading a CSV750BP-3 power supply over PMBus/I2C and could not find a PMBus command list for it. The data sheet (ENG-CSV750BP-235-01) says "PMBus compliant" but lists no commands. Could you send the PMBus command reference or register map for this unit?

Unit details
- Product: CSV750BP-3, part number [fill in from the label, for example 700-013700-1000]
- MFR_ID: EMER
- MFR_MODEL (0x9A): 00YL557
- MFR_REVISION (0x9B): 00YL556
- MFR_SERIAL (0x9E): 67M2P9
- PRIMARY_FW_REVISION (0xFB): 0x35, SECONDARY_FW_VERSION (0xFC): 0x30
- Address: 7-bit 0x68 (S15 open); 2.2 kΩ pull-ups on SDA/SCL, 100 kHz, 3.3 V logic

What I measured
Every command the unit implements returns its data followed by an SMBus PEC. These work: READ_VIN (0x88), READ_PIN (0x97), READ_EIN/READ_EOUT (0x86/0x87), READ_TEMPERATURE_1/2 (0x8D/0x8E), READ_FAN_SPEED_1 (0x90), STATUS_VOUT/IOUT/INPUT/TEMPERATURE/CML/MFR_SPECIFIC/FANS_1_2, VOUT_MODE (0x20), OPERATION, ON_OFF_CONFIG (0x15), COEFFICIENTS (0x30), MFR_* strings, and a number of vendor codes.

These return no PEC (a single stale byte, then 0xFF), so they appear not to be implemented: READ_VOUT (0x8B), READ_IOUT (0x8C), READ_IIN (0x89), READ_POUT (0x96), READ_TEMPERATURE_3 (0x8F), STATUS_BYTE (0x78), STATUS_WORD (0x79), CAPABILITY (0x19), PMBUS_REVISION (0x98), MFR_IOUT_MAX (0xA6), MFR_MAX_TEMP_1..3 (0xC0 to 0xC2) and QUERY (0x1A). Writing PAGE (0x00) did not change this. This matches neither the CSV2000BP technical reference note nor other Advanced Energy tables I have seen, which list those commands.

Questions
1. Is there a PMBus command list or register map for this firmware (00YL557 / FW 0x35, 0x30)?
2. Is READ_VOUT, READ_IOUT, READ_POUT or STATUS_WORD supported on any firmware version of the CSV750BP? If so, how do I read them on this unit, and is there a firmware update?
3. If output voltage, current and power are not exposed, which register gives output current? Register 0x6F reads 0 at no load and 1 to 2 with a 20 to 36 W load; is it output current in whole amps?
4. What do these vendor registers hold: 0x6C, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x13, 0x14, 0xD5, 0xB9 to 0xBD, 0xDE, 0xDF, 0xE1, 0xE3, 0xFA, 0xFD, 0xFE?
5. READ_EOUT (0x87) reads about the READ_EIN value plus 3 in every state I tested. What is its unit and meaning?
6. What PMBus revision does the firmware implement, and what bus timing does it require (the unit only answers within a few ms of the command write)?
7. Does PMBus work on standby alone, and which write commands are safe to use (for example CLEAR_FAULTS)?

I can send the raw captures if that would help. Thank you for your time.

Best regards,
[Name]
[Company / project]
[Email, phone]
