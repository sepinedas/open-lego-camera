# Hardware — CM4 Carrier Board

A KiCad schematic for a **Raspberry Pi Compute Module 4** carrier board that
powers the [open-lego-camera](../README.md) from a single-cell LiPo battery.

The design is drawn as a **hierarchical schematic**: a top-level sheet
(`cm4-carrier.kicad_sch`) instantiates four sub-sheets, one per functional
block.

```
cm4-carrier.kicad_sch                     (root / top level)
├── power_input.kicad_sch     USB-C in → MCP73831 LiPo charger → JST battery
├── regulator.kicad_sch       TPS61088 boost converter → 5 V rail
├── battery_monitor.kicad_sch MAX17048 I²C fuel gauge
└── cm4.kicad_sch             Raspberry Pi Compute Module 4
```

Open `cm4-carrier/cm4-carrier.kicad_pro` in **KiCad 8** (or newer) and start
from the root schematic; double-click any sheet box to descend into it.

## Power path

```
              ┌──────────────┐      ┌───────────────┐      ┌─────────────┐
  USB-C  ───▶ │  MCP73831    │      │  TPS61088     │      │    CM4      │
  5 V    VBUS │  LiPo charger│─VBAT▶│  boost → 5 V  │─+5V─▶│  (5 V in)   │
              └──────┬───────┘      └───────────────┘      └──────┬──────┘
                     │ VBAT                                       │ I²C1
              ┌──────▼───────┐                              ┌─────▼───────┐
              │ LiPo 3.7 V   │                              │  MAX17048   │
              │ 3000 mAh     │◀─────────── VBAT ────────────│ fuel gauge  │
              │ (JST-GH 1.25)│                              │  (I²C)      │
              └──────────────┘                              └─────────────┘
```

- **USB-C (J1)** supplies `VBUS` (5 V). CC1/CC2 have 5.1 kΩ pull-downs so the
  port presents as a UFP/sink.
- **MCP73831 (U1)** charges the LiPo from `VBUS`. `R3` (2 kΩ) programs the
  charge current to ≈ 500 mA; `D1` is the charge-status LED.
- **LiPo battery (J2)** — 3.7 V / 3000 mAh, single cell, on a **JST-GH 1.25 mm**
  2-pin connector. It both receives charge and powers the board (`VBAT`,
  ~3.0–4.2 V).
- **TPS61088 (U2)** boosts `VBAT` up to a regulated **5 V** for the CM4.
  `R5`/`R6` set the output via the feedback divider.
- **MAX17048 (U3)** is an I²C ModelGauge fuel gauge sensing the cell voltage
  directly on `VBAT`; it reports state-of-charge to the CM4 over **I²C1**
  (`GPIO2`/`SDA`, `GPIO3`/`SCL`), pulled up to the CM4's 3.3 V rail.

## Bill of materials

| Ref | Value | Part | Footprint |
|-----|-------|------|-----------|
| J1 | USB-C receptacle | GCT USB4110-GF-A | `Connector_USB:USB_C_Receptacle_GCT_USB4110-GF-A_16P_TopMnt_Horizontal` |
| J2 | LiPo 3.7 V 3000 mAh | JST-GH 2-pin | `Connector_JST:JST_GH_SM02B-GHS-TB_1x02-1MP_P1.25mm_Horizontal` |
| J3 | Raspberry Pi CM4 | 2× Hirose DF40C-100DS | `Connector_Hirose:Raspberry_Pi_CM4_DF40C-100DS` |
| U1 | MCP73831-2ACI/OT | LiPo charger | `Package_TO_SOT_SMD:SOT-23-5` |
| U2 | TPS61088 | 5 V boost converter | `Package_DFN_QFN:QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm` |
| U3 | MAX17048G+T | I²C fuel gauge | `Package_DFN_QFN:DFN-8-1EP_2x2mm_P0.5mm_EP0.61x1.42mm` |
| L1 | 1 µH | power inductor | `Inductor_SMD:L_Bourns-SRP1265A` |
| D1 | CHG | status LED | `LED_SMD:LED_0603_1608Metric` |
| R1, R2 | 5.1 kΩ | CC pull-downs | `Resistor_SMD:R_0402_1005Metric` |
| R3 | 2 kΩ | charge-current program | `Resistor_SMD:R_0402_1005Metric` |
| R4 | 1 kΩ | LED series | `Resistor_SMD:R_0402_1005Metric` |
| R5 | 100 kΩ | FB divider (top) | `Resistor_SMD:R_0402_1005Metric` |
| R6 | 16.2 kΩ | FB divider (bottom) | `Resistor_SMD:R_0402_1005Metric` |
| R7, R8 | 4.7 kΩ | I²C pull-ups | `Resistor_SMD:R_0402_1005Metric` |
| R9 | 10 kΩ | ALRT pull-up | `Resistor_SMD:R_0402_1005Metric` |
| C1, C2 | 4.7 µF | charger in/out | `Capacitor_SMD:C_0603_1608Metric` |
| C3 | 10 µF | boost input | `Capacitor_SMD:C_0805_2012Metric` |
| C4, C7 | 22 µF | boost output | `Capacitor_SMD:C_0805_2012Metric` |
| C5 | 10 nF | COMP | `Capacitor_SMD:C_0603_1608Metric` |
| C6 | 4.7 nF | soft-start | `Capacitor_SMD:C_0603_1608Metric` |
| C8 | 1 µF | fuel-gauge decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| C9 | 100 µF | 5 V bulk | `Capacitor_SMD:C_1206_3216Metric` |

## Notes and caveats

- **This is a schematic-only deliverable** — no PCB layout is included yet.
- **Footprints** point at KiCad's standard libraries where a standard part
  exists. Two are worth double-checking against your installed libraries and
  adjusting in *Assign Footprints* if the exact name differs:
  - The **CM4** module (`J3`) uses two Hirose `DF40C-100DS-0.4V` board-to-board
    connectors. Use the Raspberry Pi CM4 footprint from your library (or the
    official RPi KiCad library) — the symbol here is a simplified representation
    that exposes only the pins this design uses (5 V, GND, `GPIO2/SDA1`,
    `GPIO3/SCL1`, 3.3 V out).
  - The **inductor** (`L1`) footprint should match the specific high-current
    part you select for the boost converter.
- **Current budget:** boosting a 1S LiPo (3.0–4.2 V) to 5 V at the CM4's peak
  draw pulls a large input current from the cell. Size `L1`, the TPS61088, and
  the battery for your actual peak load, and verify the 3000 mAh cell's
  continuous-discharge rating covers it.
- **ERC:** power rails are fed by regulator/charger output pins, so nets have
  drivers, but KiCad's ERC may still ask for `PWR_FLAG` symbols on `VBUS`/`GND`.
  Add them if you want a fully clean ERC run.

## Regenerating

The schematic files are generated by a small script (kept out of the repo, in
the change history). They are plain KiCad 8 S-expression files and can be
edited directly in Eeschema from here on.
