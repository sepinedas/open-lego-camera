# Hardware — CM4 Carrier Board

A KiCad schematic for a **Raspberry Pi Compute Module 4** carrier board that
powers the [open-lego-camera](../README.md) from a single-cell LiPo battery,
with **USB-C charging + Dynamic Power Path Management** and a dedicated
**battery-protection** stage.

The design is drawn as a **hierarchical schematic**: a top-level sheet
(`cm4-carrier.kicad_sch`) instantiates five sub-sheets, one per functional
block.

```
cm4-carrier.kicad_sch                        (root / top level)
├── battery_protection.kicad_sch AP9101CK6 + FS8205A cell protection
├── power_input.kicad_sch         USB-C in → BQ24075 DPPM charger → SYS
├── regulator.kicad_sch           TPS61088 boost converter → 5 V rail
├── battery_monitor.kicad_sch     MAX17048 I²C fuel gauge
└── cm4.kicad_sch                 Raspberry Pi Compute Module 4
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
  (`GPIO2`/`SDA`, `GPIO3`/`SCL`), pulled up to the CM4's 3.3 V rail.

## Bill of materials

| Ref | Value | Part | Footprint |
|-----|-------|------|-----------|
| J1 | USB-C receptacle | GCT USB4110-GF-A | `Connector_USB:USB_C_Receptacle_GCT_USB4110-GF-A_16P_TopMnt_Horizontal` |
| J2 | LiPo 3.7 V 3000 mAh | JST-GH 2-pin | `Connector_JST:JST_GH_SM02B-GHS-TB_1x02-1MP_P1.25mm_Horizontal` |
| J3 | Raspberry Pi CM4 | 2× Hirose DF40C-100DS | `Connector_Hirose:Raspberry_Pi_CM4_DF40C-100DS` |
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

## Notes and caveats

- **This is a schematic-only deliverable** — no PCB layout is included yet.
- **Belt-and-suspenders protection.** The AP9101/FS8205 stage protects a *bare*
  cell. If you use a LiPo that already ships with an integrated protection PCM,
  this stage is redundant (harmless, but you can depopulate `U4`/`Q1` and link
  `B-`→`GND`). The BQ24075's `TS` thermistor and the fuel gauge's over/under
  voltage alerts add further layers.
- **Footprints** point at KiCad's standard libraries where a standard part
  exists. Worth double-checking against your installed libraries and adjusting
  in *Assign Footprints* if a name differs:
  - The **CM4** module (`J3`) uses two Hirose `DF40C-100DS-0.4V` board-to-board
    connectors. Use the Raspberry Pi CM4 footprint from your library — the symbol
    here is a simplified representation exposing only the pins this design uses
    (5 V, GND, `GPIO2/SDA1`, `GPIO3/SCL1`, 3.3 V out).
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
