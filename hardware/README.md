# Open Lego Camera — CM4 Carrier Board (KiCad)

A KiCad schematic for a **Raspberry Pi Compute Module 4 (CM4)** carrier board
that hosts the [open-lego-camera](../README.md) application: dual cameras, an
LCD, on-board audio, a LiPo battery with charging and fuel-gauge monitoring,
and a handful of user buttons/LEDs on a Dupont header.

| File | Purpose |
| --- | --- |
| `open-lego-camera-cm4.kicad_sch` | The schematic (all symbols embedded — no external libraries required) |
| `open-lego-camera-cm4.kicad_pro` | KiCad 7/8 project file |
| `open-lego-camera-cm4.kicad_pcb` | PCB: layer stack, all nets, board outline, top + bottom GND ground planes |
| `build_pcb.py` | KiCad 10 `pcbnew` script: places parts, draws outline, pours GND planes, exports DSN + imports routed SES |
| `fp-lib-table` | Registers the local footprint library (needed for the CM4 placeholder) |
| `open-lego-camera.pretty/CM4_Functional.kicad_mod` | Placeholder CM4 land pattern; pads match the symbol pins so every CM4 net maps |

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

## Connections & footprints

Every component pin is wired to a net (no floating functional pins), and
**every component is assigned a standard KiCad footprint**. Highlights of the
completed wiring:

- **USB-C data** (`USB_DP`/`USB_DN`) is routed to the CM4 USB pins, and the CC
  lines have 5.1 kΩ sink pulldowns.
- **Charger straps:** `CE` tied high (charging enabled), `TE` low (safety timer
  off), `PROG1`/`PROG2` resistors set the fast-charge and USB input-current
  limits. `STAT1`, `STAT2` and `PG` drive **status LEDs** `D1`/`D2`/`D3`.
- **Fuel gauge** `QSTRT` tied low; `FUEL_ALRT` routed to CM4 `GPIO24`.
- **Camera control** — each camera's enable/LED lines go to CM4 GPIOs
  (`GPIO4/27` enable, `GPIO22/23` LED); camera/display I²C shares `CAM_SDA/SCL`
  with 1.8 kΩ pull-ups.
- **Amp** `GAIN` set with a 100 kΩ-to-GND resistor (12 dB); `SD_MODE` pulled to
  +5 V (enabled, left/right averaged).
- **Boot control** — `GLOBAL_EN` and `nRPIBOOT` pulled up; `J9` is a jumper that
  grounds `nRPIBOOT` to force USB (rpiboot) flashing mode.
- **Test points** `TP1`–`TP3` expose the CM4 `+3V3` output, `RUN_PG`, and
  `SD_VDD_EN`.

## Notes & assumptions

- The CM4 is drawn as a **functional symbol** (`U5`) exposing the power rails
  and the specific GPIO / SDIO / CSI / DSI / I²S / USB pins used here, rather
  than the two full 100-pin Hirose DF40 board connectors. This keeps the
  schematic readable; the assigned footprint is a single DF40 100-pin connector
  as a placeholder. **Before fabricating a PCB, expand `U5` to the two physical
  100-pin connectors and verify every pin against the official CM4 datasheet.**
- "LCD **SDI**" in the brief is interpreted as the CM4 **DSI** (Display Serial
  Interface) — the standard way to attach a Raspberry Pi LCD panel.
- Camera/display connectors are the common **15-pin 1 mm FFC** Pi camera/display
  pinout. If you use the 22-pin 0.5 mm flat-flex found on CM4 IO boards / Pi Zero,
  swap the connectors and re-check the pinout.
- Symbols are simplified rectangular representations with the correct pin
  **names/functions** for schematic capture. Footprints reference the standard
  KiCad 7/8 libraries; confirm each against your chosen manufacturer part, and
  double-check the `ICS-43434` (mapped to a Knowles LGA-6 land pattern) and the
  `MAX17048` package before ordering.

## PCB

`open-lego-camera-cm4.kicad_pcb` is a 2-layer board that already contains:

- the full copper/silk/mask/paste **layer stack**;
- **every net** from the schematic (matched by name, GND = net 1);
- a **100 × 80 mm board outline** on `Edge.Cuts`;
- **GND ground-plane zones on both copper layers** (`F.Cu` and `B.Cu`).

### Completing footprints + routing (KiCad 10)

Copper **routes** and the **footprint pad geometry** are not written into the
file here — routing traces connect real pad coordinates, and both the pads and
the copper pour are produced by KiCad's engine from your installed footprint
libraries. Finish the board in KiCad 10:

1. Open the project and the schematic; run **Tools ▸ Update PCB from Schematic**
   (`F8`). KiCad pulls in all footprints with correct pads and the ratsnest,
   matching the nets already in the board (including GND). The bundled
   `fp-lib-table` registers the local `open-lego-camera.pretty` library so the
   CM4 placeholder footprint resolves — if KiCad prompts about the project
   library table, accept it (or restart KiCad after pulling these files).
2. Run **`build_pcb.py`** to place the parts by subsystem, (re)draw the outline,
   pour both GND planes, and export the router job. In the PCB editor's
   **Tools ▸ Scripting Console**:

   ```
   exec(open('hardware/build_pcb.py').read())
   ```

3. **Route** the board. `pcbnew` has no built-in autorouter, so the script uses
   [Freerouting](https://github.com/freerouting/freerouting) (needs Java):
   set `FREEROUTING_JAR=/path/to/freerouting.jar` before step 2 and it will
   autoroute and import the result automatically. Otherwise the script exports
   `open-lego-camera-cm4.dsn`; route it in Freerouting and bring it back with
   **File ▸ Import ▸ Specctra Session** (`.ses`). You can also route by hand.
4. The script re-runs **Fill All Zones** after routing; press `B` any time to
   re-pour. Then run **DRC** (Inspect ▸ Design Rules Checker) and fix issues
   before fabrication.

> `build_pcb.py` targets the KiCad 10 `pcbnew` API (it also runs on 8/9 — the
> shared calls are used and version-sensitive ones are guarded). It must run
> inside KiCad; `pcbnew` is not a standalone package.

> Reminder: `U5` (CM4) uses `open-lego-camera:CM4_Functional`, a **placeholder**
> land pattern whose 54 pads are named to match the functional symbol pins so
> every CM4 net maps and routes. It is **not** the real CM4 mechanical
> footprint — before fabricating, replace it with the two physical 100-pin
> Hirose DF40 connectors and verify every pin against the CM4 datasheet.

All other footprints reference stock KiCad 10 libraries and were validated
against the official KiCad footprint repository (the connectors, the ICS-43434
mic, the DFN/QFN ICs, the Bourns inductor, and the 15-pin 1 mm FFC connectors).

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
| J9 | 2-pin jumper | rpiboot / USB-flash mode select |
| D1–D3 | LED | Charge / standby / power-good status |
| TP1–TP3 | Test point | CM4 +3V3 out, RUN_PG, SD_VDD_EN |
