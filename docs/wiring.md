# Wiring

This is the home bench version. The lab test used the same pins.

## CN0276 connector J2 (20-pin IDC)

Read from the EVAL-CN0276-SDPZ schematic.

| J2 pin | Signal | J2 pin | Signal |
|---|---|---|---|
| 1 | +3.3 V | 11 | GND |
| 2 | A0 | 12 | SDI |
| 3 | A1 | 13 | SDO |
| 4 | A | 14 | GND |
| 5 | B | 15 | SAMPLE |
| 6 | NM | 16 | SOE |
| 7 | DIR | 17 | WR/FSYNC |
| 8 | LOT | 18 | RES1 |
| 9 | DOS | 19 | RES0 |
| 10 | SCLK | 20 | GND |

## LaunchPad to CN0276

| C2000 pin | Function | Goes to |
|---|---|---|
| GPIO58 | SPISIMOA | SDI (J2-12) |
| GPIO59 | SPISOMIA | SDO (J2-13) |
| GPIO60 | SPICLKA | SCLK (J2-10) |
| GPIO61 | SPISTEA | WR/FSYNC (J2-17) |
| GPIO19 (J1 pin 3) | GPIO output | A0 or A1 |
| GPIO32 (J1 pin 2) | GPIO output | A0 or A1 |
| GPIO40 (J5 pin 50) | GPIO output | SAMPLE (J2-15) |
| GND | | GND |

## Notes

- GPIO19 and GPIO32 go to A0 and A1. The code always drives both together (11 for configuration mode, 00 for normal mode), so which goes where does not matter.
- SOE (J2-16) is tied to GND to select the serial interface. The board pulls it high by default.
- The board pulls CS low itself.
- RES0 and RES1 were left unconnected in all tests. The pin levels were not measured with a meter. The position readings point to 16-bit output (16223 counts has its two lowest bits set).
- SPI-A on GPIO58 to 61 is mux option 15. That is GMUX = 3 and MUX = 3 for each pin. Both have to be set (see lesson 2 in [lessons.md](lessons.md)).
- No pull resistors were fitted on the mode or framing lines. See lesson 12.
