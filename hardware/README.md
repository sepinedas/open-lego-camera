# Hardware — CM4 Carrier Board

A KiCad schematic for a **Raspberry Pi Compute Module 4** carrier board that
powers the [open-lego-camera](../README.md) from a single-cell LiPo battery,
with **USB-C charging + Dynamic Power Path Management**, a dedicated
**battery-protection** stage, **two CSI-2 cameras**, a **DSI display**, and a
**microSD card with CM4 bring-up controls** (for booting a CM4 Lite).

The design is drawn as a **hierarchical schematic**: a top-level sheet
(`cm4-carrier.kicad_sch`) instantiates seven sub-sheets, one per functional
block.

```
cm4-carrier.kicad_sch                        (root / top level)
├── battery_protection.kicad_sch AP9101CK6 + FS8205A cell protection
├── power_input.kicad_sch         USB-C in → BQ24075 DPPM charger → SYS
├── regulator.kicad_sch           TPS61088 boost converter → 5 V rail
├── battery_monitor.kicad_sch     MAX17048 I²C fuel gauge
├── cm4.kicad_sch                 Raspberry Pi CM4 (2× DF40C-100DP connectors)
├── cameras.kicad_sch             2× CSI-2 camera + 1 DSI display + PCA9544A mux
└── boot_sd.kicad_sch             microSD (CM4 Lite boot) + bring-up controls
```

Open `cm4-carrier/cm4-carrier.kicad_pro` in **KiCad 10** (or newer) and start
from the root schematic; double-click any sheet box to descend into it.

## Power path (with DPPM)

```
                          ┌──────────── BQ24075 (U1) ────────────┐
   USB-C ──VBUS──▶ IN     │  Dynamic Power Path Management        │
   (5 V)                  │  • OUT powers the system directly     │──SYS──▶ TPS61088 ──+5V──▶ CM4 (J3)
                          │    from USB when present              │        boost (U2)
                          │  • falls back to VBAT when USB is out │
                          │  • charges the cell independently     │
                          └───────▲──────────────────┬───────────┘
                            VBAT  │                   │ TS (10k NTC = batt temp)
                    ┌─────────────┴──────────┐        │
                    │  Battery protection    │        └──────────────┐
   LiPo cell ──B+──▶│  AP9101CK6 + FS8205A   │──VBAT──▶ MAX17048 (U3) fuel gauge
   3.7 V 3000 mAh   │  OV / UV / OC / SC     │           (I²C → CM4 GPIO2/3)
   (JST-GH 1.25) B- ▶│  cutoff in B- line    │
                    └────────────────────────┘
```

- **USB-C (J1)** supplies `VBUS` (5 V). CC1/CC2 have 5.1 kΩ pull-downs so the
  port presents as a UFP/sink.
- **BQ24075 (U1)** is a USB-friendly linear charger with **Dynamic Power Path
  Management**. Its `OUT` pin is a separate **system rail (`SYS`)**: the load is
  powered *directly from USB* whenever it is present (so the system runs even
  with a depleted/absent battery and the cell isn't needlessly cycled), and
  seamlessly **falls back to the battery (`VBAT`)** when USB is removed. The cell
  charges from the remaining input budget.
  - `R3` (ISET) sets the charge current, `R4` (ILIM) the input current limit,
    `R10` (TMR) the safety timer.
  - `EN1`/`EN2` are tied high to select the resistor-programmed input limit;
    `/CE` and `SYSOFF` are tied low (charge + power-path enabled).
  - **`TS`** senses battery temperature via a 10 kΩ NTC (`RT1`, mounted at the
    cell) with bias `R11` — the charger suspends charging outside the safe
    temperature window.
  - `D1` (`/CHG`) and `D2` (`/PGOOD`) are status LEDs.
- **Battery protection (U4 + Q1)** — an **AP9101CK6** 1-cell protection
  controller (a DW01A-class part) driving an **FS8205A** dual N-MOSFET in the
  cell's negative line. It disconnects the cell on **over-charge, over-discharge,
  over-current, and short-circuit**. The protected terminals become the board's
  `VBAT` (+) and `GND` (−); note the raw cell negative (`B-`) is *not* board
  ground — only the protected `P-` is.
- **LiPo battery (J2)** — 3.7 V / 3000 mAh, single cell, on a **JST-GH 1.25 mm**
  2-pin connector, wired to the protection stage.
- **TPS61088 (U2)** boosts `SYS` (≈3.0–4.4 V) up to a regulated **5 V** for the
  CM4. `R5`/`R6` set the output via the feedback divider.
- **MAX17048 (U3)** is an I²C ModelGauge fuel gauge sensing the protected cell
  voltage on `VBAT`; it reports state-of-charge to the CM4 over **I²C1**
  (`GPIO2`/`SDA`, `GPIO3`/`SCL`), pulled up to the CM4's `CM4_3.3V_Out` rail.
- **Compute Module 4 (J3, J4)** — the module is modeled as its two real 100-pin
  **Hirose DF40C-100DP-0.4V** mating connectors (J3 = connector 1, J4 =
  connector 2) with the full, correctly-numbered CM4 pinout. This board wires
  what it needs: all six **+5V** input pins, every **GND**, **GPIO2/GPIO3**
  (I²C1) to the fuel gauge, and **`GPIO_VREF` + `CM4_3.3V_Out`** for the 3.3 V
  I/O rail. The high-speed interfaces (PCIe, USB, Ethernet, HDMI, CSI/DSI camera
  & display) are left available on the connectors for future expansion.
- **Two CSI-2 cameras (J5, J6)** on standard 15-pin **1.0 mm Raspberry Pi camera
  FFC** connectors. `J5` uses the CM4's **CAM0** (2-lane) interface and `J6` uses
  **CAM1** (4-lane; two lanes wired, enough for standard Pi cameras). Because
  both cameras answer at the same I²C address, a **PCA9544A I²C mux (U5)** sits on
  the camera control bus (**I²C0**, from `I2C_SCL0`/`I2C_SDA0`) so each camera is
  on its own mux channel. A local **AP2112K-3.3 LDO (U7)** supplies the camera
  3.3 V rail from `+5V` (keeping the load off the CM4's limited `CM4_3.3V_Out`),
  and each camera's `IO0`/`IO1` (LED / power-enable) route to spare CM4 GPIOs
  (GPIO4–7). The CM4↔camera CSI/I²C0 nets use **global labels**.
- **DSI display (J7)** on the same 15-pin 1.0 mm FFC, driven by the CM4's
  **DSI0** (2-lane) interface. Its touch/controller I²C shares the I²C0 bus on
  **mux channel 2**, its `IO0`/`IO1` (reset / backlight-enable) route to CM4
  GPIO8/GPIO9, and its 3.3 V logic comes from the same camera rail. As with the
  standard Raspberry Pi 7″ panel, the display's **5 V panel/backlight power is
  supplied separately** — the FFC carries only DSI + I²C + 3.3 V logic + control.
- **microSD boot + CM4 bring-up (J8 + controls).** A microSD socket (`J8`) is
  wired to the CM4's SD interface (`SD_CLK`/`SD_CMD`/`SD_DAT0-3`) so a **CM4 Lite**
  (no eMMC) boots from card. The card's 3.3 V is gated by a high-side load
  switch (`Q2`/`Q3`) driven by the CM4's `SD_PWR_ON`, with line pull-ups and a
  card-detect pull-up. The module bring-up pins are broken out to controls:
  **`GLOBAL_EN`** (power/enable, `SW1`), **`RUN_PG`** (run/reset, `SW2`) and
  **`nRPIBOOT`** (USB/rpiboot mode, `SW3`) each with a pull-up and momentary
  button, plus **power/activity LEDs** (`D3`/`D4` on `PI_LED_nPWR` /
  `PI_LED_ACT`) and a pulled-up `nEXTRST`. All CM4↔boot nets use global labels.

## Bill of materials

| Ref | Value | Part | Footprint |
|-----|-------|------|-----------|
| J1 | USB-C receptacle | GCT USB4110-GF-A | `Connector_USB:USB_C_Receptacle_GCT_USB4110-GF-A_16P_TopMnt_Horizontal` |
| J2 | LiPo 3.7 V 3000 mAh | JST-GH 2-pin | `Connector_JST:JST_GH_SM02B-GHS-TB_1x02-1MP_P1.25mm_Horizontal` |
| J3, J4 | Raspberry Pi CM4 (mating) | 2× Hirose DF40C-100DP-0.4V | `Connector_Hirose_DF40:Hirose_DF40C-100DP-0.4V_2x50-1MP_P0.4mm` |
| J5, J6 | CSI-2 camera FFC | 15-pin 1.0 mm (JUSHUO AFA07) | `Connector_FFC-FPC:JUSHUO_AFA07-S15FCA-00_1x15-1MP_P1.0mm_Horizontal` |
| J7 | DSI display FFC | 15-pin 1.0 mm (JUSHUO AFA07) | `Connector_FFC-FPC:JUSHUO_AFA07-S15FCA-00_1x15-1MP_P1.0mm_Horizontal` |
| U5 | PCA9544APW | 4-ch I²C mux (3 used) | `Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm` |
| U7 | AP2112K-3.3 | camera 3.3 V LDO | `Package_TO_SOT_SMD:SOT-23-5` |
| J8 | microSD socket | Hirose DM3D-SF (push-push) | `Connector_Card:microSD_HC_Hirose_DM3D-SF` |
| Q2 | DMG2305UX | SD power P-MOSFET (high-side) | `Package_TO_SOT_SMD:SOT-23` |
| Q3 | 2N7002 | SD switch N-MOSFET | `Package_TO_SOT_SMD:SOT-23` |
| SW1–SW3 | tact switch | GLOBAL_EN / RUN / nRPIBOOT | `Button_Switch_THT:SW_PUSH_6mm` |
| D3, D4 | PWR / ACT | status LEDs | `LED_SMD:LED_0603_1608Metric` |
| U1 | BQ24075RGT | DPPM Li-ion charger | `Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.7x1.7mm` |
| U2 | TPS61088 | 5 V boost converter | `Package_DFN_QFN:QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm` |
| U3 | MAX17048G+T | I²C fuel gauge | `Package_DFN_QFN:DFN-8-1EP_2x2mm_P0.5mm_EP0.61x1.42mm` |
| U4 | AP9101CK6 | 1-cell protection IC | `Package_TO_SOT_SMD:SOT-23-6` |
| Q1 | FS8205A | dual N-MOSFET (protection) | `Package_TO_SOT_SMD:SOT-23-6` |
| L1 | 1 µH | power inductor | `Inductor_SMD:L_Bourns-SRP1265A` |
| D1 | CHG | charge-status LED | `LED_SMD:LED_0603_1608Metric` |
| D2 | PGOOD | power-good LED | `LED_SMD:LED_0603_1608Metric` |
| RT1 | 10 kΩ NTC | battery thermistor (TS) | `Resistor_SMD:R_0603_1608Metric` |
| R1, R2 | 5.1 kΩ | CC pull-downs | `Resistor_SMD:R_0402_1005Metric` |
| R3 | 1.1 kΩ | ISET (charge current) | `Resistor_SMD:R_0402_1005Metric` |
| R4 | 1.1 kΩ | ILIM (input current limit) | `Resistor_SMD:R_0402_1005Metric` |
| R5 | 100 kΩ | boost FB divider (top) | `Resistor_SMD:R_0402_1005Metric` |
| R6 | 16.2 kΩ | boost FB divider (bottom) | `Resistor_SMD:R_0402_1005Metric` |
| R7, R8 | 4.7 kΩ | I²C pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R9 | 10 kΩ | ALRT pull-up | `Resistor_SMD:R_0402_1005Metric` |
| R10 | 68 kΩ | TMR (safety timer) | `Resistor_SMD:R_0402_1005Metric` |
| R11 | 10 kΩ | TS bias | `Resistor_SMD:R_0402_1005Metric` |
| R12, R13 | 1 kΩ | status-LED series | `Resistor_SMD:R_0402_1005Metric` |
| R14 | 330 Ω | protection VDD series | `Resistor_SMD:R_0402_1005Metric` |
| R15, R16 | 4.7 kΩ | camera 0 I²C pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R17, R18 | 4.7 kΩ | camera 1 I²C pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R19, R20 | 4.7 kΩ | I²C0 (mux upstream) pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R21, R22 | 4.7 kΩ | DSI display I²C pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R23–R27 | 51 kΩ | SD CMD/DAT0-3 pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R28 | 51 kΩ | SD card-detect pull-up | `Resistor_SMD:R_0402_1005Metric` |
| R29 | 100 kΩ | SD switch gate pull-up | `Resistor_SMD:R_0402_1005Metric` |
| R30, R31 | 100 kΩ | SD_PWR_ON / SD_VDD_Override defaults | `Resistor_SMD:R_0402_1005Metric` |
| R32 | 100 kΩ | GLOBAL_EN pull-up | `Resistor_SMD:R_0402_1005Metric` |
| R33–R35 | 10 kΩ | RUN / nRPIBOOT / nEXTRST pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R36, R37 | 1 kΩ | status-LED series | `Resistor_SMD:R_0402_1005Metric` |
| C1 | 1 µF | charger IN decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| C2 | 10 µF | SYS (OUT) decoupling | `Capacitor_SMD:C_0805_2012Metric` |
| C10 | 10 µF | BAT decoupling | `Capacitor_SMD:C_0805_2012Metric` |
| C3 | 10 µF | boost input | `Capacitor_SMD:C_0805_2012Metric` |
| C4, C7 | 22 µF | boost output | `Capacitor_SMD:C_0805_2012Metric` |
| C5 | 10 nF | COMP | `Capacitor_SMD:C_0603_1608Metric` |
| C6 | 4.7 nF | soft-start | `Capacitor_SMD:C_0603_1608Metric` |
| C8 | 1 µF | fuel-gauge decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| C9 | 100 µF | 5 V bulk | `Capacitor_SMD:C_1206_3216Metric` |
| C11 | 0.1 µF | protection VDD decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| C12 | 10 µF | CM4 5 V decoupling | `Capacitor_SMD:C_0805_2012Metric` |
| C13 | 0.1 µF | I²C mux decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| C14, C15 | 1 µF | camera LDO in/out | `Capacitor_SMD:C_0603_1608Metric` |
| C16 | 1 µF | SD card bulk | `Capacitor_SMD:C_0603_1608Metric` |
| C17 | 100 nF | SD card decoupling | `Capacitor_SMD:C_0603_1608Metric` |

## Notes and caveats

- **This is a schematic-only deliverable** — no PCB layout is included yet.
- **Belt-and-suspenders protection.** The AP9101/FS8205 stage protects a *bare*
  cell. If you use a LiPo that already ships with an integrated protection PCM,
  this stage is redundant (harmless, but you can depopulate `U4`/`Q1` and link
  `B-`→`GND`). The BQ24075's `TS` thermistor and the fuel gauge's over/under
  voltage alerts add further layers.
- **CM4 connectors & bring-up.** `J3`/`J4` are the **DF40C-100DP-0.4V** parts on
  the *carrier* that mate with the CM4's DF40C-100DS sockets; pick the stacking
  height (DF40C = 1.5 mm, DF40HC = 3.0 mm) to suit your mechanical stack. Because
  this is a focused carrier, the CM4 pins it does not use (many GPIOs, the
  second HDMI, PCIe, USB, Ethernet, etc.) are intentionally left unconnected and
  **will show as unconnected in ERC** — that is expected. The boot/control pins
  (`GLOBAL_EN`, `RUN_PG`, `nRPIBOOT`, `nEXTRST`, the SD interface and the LED
  outputs) and the CSI/DSI interfaces *are* wired — see the boot and camera
  sheets.
- **CM4 Lite vs eMMC / SD power budget.** The microSD boot path targets a **CM4
  Lite**; on an eMMC CM4 the card is an optional extra boot source. The SD card
  is powered from the CM4's `CM4_3.3V_Out` through the `SD_PWR_ON` load switch —
  that rail is current-limited, so if you stack heavy 3.3 V loads (SD + both
  cameras + display logic) budget it and add a dedicated 3.3 V regulator if
  needed. The bring-up buttons are momentary: `SW1` (GLOBAL_EN) forces the module
  off while held, `SW2` (RUN) resets it, and holding `SW3` (nRPIBOOT) at power-up
  enters USB/rpiboot mode for flashing.
- **Cameras & display.** The three connectors take standard 15-pin 1.0 mm Pi
  FFC cables. The PCA9544A mux is at I²C address `0x70` (A0–A2 tied low): cam0 =
  channel 0, cam1 = channel 1, DSI display = channel 2 — reflect this in your
  `dtoverlay`/libcamera setup (each device on its own downstream bus). The
  `IO0`/`IO1` control lines route to CM4 GPIO4–9 and are assignable in software;
  tie or reassign them to match your specific camera/display modules. CAM1's and
  DSI0's unused lanes are left on the CM4 connector. The display's 5 V panel
  power is separate (see above).
- **Footprints** otherwise point at KiCad's standard libraries. Worth
  double-checking in *Assign Footprints* if a name differs:
  - The **inductor** (`L1`) footprint should match the specific high-current
    part you select for the boost converter.
- **Current budget:** boosting a 1S LiPo to 5 V at the CM4's peak draw pulls a
  large input current from the cell. Size `L1`, the TPS61088, the FS8205A, and
  the cell for your actual peak load, and set `R3`/`R4` accordingly. DPPM helps
  here — when USB is present the boost draws from `SYS`/USB rather than the cell.
- **ERC:** power rails are fed by regulator/charger output pins, so nets have
  drivers, but KiCad's ERC may still ask for `PWR_FLAG` symbols on `VBUS`/`GND`.
  The unused USB `D+`/`D-` stubs will also flag as unconnected. Add `PWR_FLAG`s
  and `no_connect` markers if you want a fully clean ERC run.

## Regenerating

The schematic files are generated by a small script (kept out of the repo, in
the change history). They are plain KiCad 10 S-expression files (schematic
format `20260306`) and can be edited directly in Eeschema from here on.
