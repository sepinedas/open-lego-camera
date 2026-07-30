# Open Lego Camera — CM4 Carrier Board (KiCad)

A KiCad schematic for a **Raspberry Pi Compute Module 4 (CM4)** carrier board
that hosts the [open-lego-camera](../README.md) application: dual cameras, an
LCD, on-board audio, a LiPo battery with charging and fuel-gauge monitoring,
and a handful of user buttons/LEDs on a Dupont header.

| File | Purpose |
| --- | --- |
| `open-lego-camera-cm4.kicad_sch` | The schematic (all symbols embedded — no external libraries required) |
| `open-lego-camera-cm4.kicad_pro` | KiCad 7/8 project file |

Open it with **KiCad 7 or 8**: `File ▸ Open Project…` → `open-lego-camera-cm4.kicad_pro`,
or open the `.kicad_sch` directly in the schematic editor.

## What's on the board

The design maps directly onto the request for the carrier:

| Requirement | Implementation | Ref |
| --- | --- | --- |
| Battery **system management** (power path) | MCP73871 load-sharing power-path controller — runs the system from USB when present, from the battery otherwise, and charges while running | `U1` |
| Battery **charge monitor** | MAX17048 single-cell I²C fuel gauge (state-of-charge, voltage, low-battery alert) | `U2` |
| **Voltage regulator** | TPS63020 buck-boost → **+5 V** system/CM4 rail; AP2112K LDO → **+3 V3** peripheral rail | `U3`, `U4` |
| **LiPo battery socket** | JST-PH 1S connector | `J2` |
| **Charge battery via USB-C** | USB Type-C receptacle (VBUS/CC/D±) feeding the MCP73871 charger; 5.1 kΩ CC pulldowns for UFP/sink | `J1`, `R1`, `R2` |
| **microSD input + socket** | microSD push-pull socket on the CM4 SDIO bus (for CM4 *Lite*, no on-module eMMC) | `J3` |
| **2× Raspberry Pi camera** connections + sockets | Two 15-pin CSI FFC connectors on CM4 CAM0 / CAM1 (2 data lanes + clock each, shared camera I²C) | `J4`, `J5` |
| **1× LCD DSI interface** | 15-pin DSI display FFC connector on CM4 DSI0 (2 data lanes + clock, I²C for touch/backlight) | `J6` |
| **2× ICS-43434 microphones** | Two I²S MEMS mics sharing BCLK/LRCLK/data; L/R pin strapped so one is the left and one the right channel | `U6`, `U7` |
| **1× MAX98357A amplifier** | I²S class-D amp → speaker socket | `U8`, `J7` |
| **IO header for 3 buttons + 2 LEDs (Dupont)** | 2.54 mm header; buttons pull GPIOs low (on-board 10 kΩ pull-ups), LEDs driven through on-board 330 Ω resistors | `J8`, `R9`–`R16` |

## Power tree

```
USB-C VBUS ─┐
            ├─► MCP73871 (U1) ─► VSYS ─► TPS63020 (U3) ─► +5V ─► CM4, MAX98357
LiPo (J2) ──┘        │                                    └──► AP2112K (U4) ─► +3V3 ─► mics, cameras, SD, pull-ups
                     └─► MAX17048 (U2) fuel gauge (I²C, on VBAT)
```

## Signal routing (net map)

The schematic uses **label-based connectivity**: every pin has a short wire
stub and a **global label**, and two labels with the same name are the same
net anywhere on the sheet. Key nets:

- **Power:** `VBUS_USB`, `VBAT`, `VSYS`, `+5V`, `+3V3`, `GND`
- **I²C (fuel gauge / user):** `I2C_SDA` (GPIO2), `I2C_SCL` (GPIO3)
- **Camera/display control I²C:** `CAM_SDA`, `CAM_SCL` (with 1.8 kΩ pull-ups)
- **I²S audio:** `I2S_BCLK` (GPIO18), `I2S_LRCLK` (GPIO19), `I2S_DIN` (GPIO20, mics),
  `I2S_DOUT` (GPIO21, amp)
- **microSD (SDIO):** `SD_CLK`, `SD_CMD`, `SD_DAT0..3`, `SD_CD`
- **CSI0/CSI1:** `CAM0_*` / `CAM1_*` differential pairs (`_DP0/_DN0`, `_DP1/_DN1`, `_CP/_CN`)
- **DSI0:** `DSI0_*` differential pairs
- **User IO:** `BTN1..3` / `BTN*_GPIO` (GPIO5/6/13), `LED1..2` / `LED*_GPIO` (GPIO16/26)

## Notes & assumptions

- The CM4 is drawn as a **functional symbol** (`U5`) exposing the power rails
  and the specific GPIO / SDIO / CSI / DSI / I²S pins used here, rather than the
  two full 100-pin Hirose DF40 board connectors. This keeps the schematic
  readable; **before fabricating a PCB, expand it to the two physical 100-pin
  connectors and verify every pin against the official CM4 datasheet.**
- "LCD **SDI**" in the brief is interpreted as the CM4 **DSI** (Display Serial
  Interface) — the standard way to attach a Raspberry Pi LCD panel.
- Camera/display connectors are the common **15-pin 1 mm FFC** Pi camera/display
  pinout. If you use the 22-pin 0.5 mm flat-flex found on CM4 IO boards / Pi Zero,
  swap the connectors and re-check the pinout.
- Symbols are simplified rectangular representations with the correct pin
  **names/functions** for schematic capture; **footprints are intentionally left
  unassigned** and must be added before layout.
- ERC will flag the intentionally-optional signals left available as test points
  (charger `STAT`/`PG`, `FUEL_ALRT`, camera `GPIO`/`LED`, CM4 `RUN_PG`, the CM4
  `+3V3` output, etc.). Assign footprints and complete these connections for a
  production board.

## Bill of materials (key parts)

| Ref | Part | Function |
| --- | --- | --- |
| U1 | MCP73871 | USB power-path + LiPo charger |
| U2 | MAX17048 | Fuel gauge / charge monitor |
| U3 | TPS63020 | Buck-boost regulator → +5 V |
| U4 | AP2112K-3.3 | LDO → +3 V3 |
| U5 | Raspberry Pi CM4 | Compute module |
| U6, U7 | ICS-43434 | I²S MEMS microphones |
| U8 | MAX98357A | I²S class-D amplifier |
| J1 | USB-C receptacle | Charge + data |
| J2 | JST-PH 1S | LiPo battery socket |
| J3 | microSD socket | Storage |
| J4, J5 | 15-pin CSI FFC | Cameras |
| J6 | 15-pin DSI FFC | LCD |
| J7 | 2-pin socket | Speaker |
| J8 | 2.54 mm header | Buttons + LEDs (Dupont) |
