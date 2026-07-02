# Sheet 01_POWER — Design Notes (Rev A)

Companion to `01_POWER.kicad_sch`. Topology is fixed by the project documents and
NOT changed here: **24 V in → PTC → PMOS reverse protection → TVS → 24 V→5 V buck
→ 5 V→3.3 V buck → protected sensor power**. Input range is 12–24 VDC
(`docs/00_PROJECT_BIBLE.md`). Net names follow the existing convention.

> **Rev history:** Rev A.2 incorporates the power review — F1 resized, 3.3 V
> stage changed LDO→buck, sensor rail defaults to +5 V (selectable), UVLO
> recalculated, front-end FET to 100 V, input TVS to SMBJ30A, SW test point DNP.

## Power budget (drives F1 and thermal)

Worst case at the **12 V** end: 5 V rail ≈ 2 A (3.3 V buck input + relays + RS485
+ sensor + LEDs). 5 V·2 A = 10 W; buck η ≈ 85 % → ~11.8 W in / 12 V ≈ **~1.0 A
input** (peak ~1.2 A). PTC hold current derates with ambient (~×0.6 at 60 °C), so
F1 must be rated well above 1.2 A.

- **F1 = Bourns MF-R250 (2.5 A hold, ~5 A trip)**. Sizing with ambient derating:
  worst-case input ≈ 1.2 A peak; PTC hold derates ~×0.6 at 60 °C, so the rated
  hold must be ≥ 1.2 A / 0.6 ≈ **2.0 A**. MF-R150 (1.5 A → ~0.9 A derated) would
  nuisance-trip; **MF-R250 (2.5 A → ~1.5 A derated)** clears the 1.2 A peak with
  margin while ~5 A trip still protects the field wiring. Confirm against the
  final enclosure ambient and the measured full-load current.

## Power chain & net list

| Stage | Ref | Device | In → Out |
|-------|-----|--------|----------|
| Input terminal | J1 | screw 2P 5.08 mm | — → VIN_24V, GND |
| PTC fuse | F1 | MF-R150 (1.5 A) | VIN_24V → VIN_F |
| Reverse polarity | Q1 | **P-FET 100 V** (DMP10H700S) S=load, D=supply | VIN_F → VIN_PROTECTED |
| Gate net | R1, D2 | 100 k gate→GND, SMBJ15CA G-S clamp | RPP_G |
| Input TVS | D1 | **SMBJ30A** (30 V standoff, ~48 V clamp) | VIN_PROTECTED → GND |
| Bulk/decouple | C1,C2,C3 | 100 µF/50 V, 10 µF/50 V, 100 nF | VIN_PROTECTED → GND |
| 24→5 V buck | U1 | **TI TPS54360** (60 V, 3.5 A) + support | VIN_PROTECTED → +5 V |
| 5→3.3 V buck | U2 | **TI TPS563201** (sync buck) + L2/FB/EN | +5 V → +3V3 |
| Sensor source sel | R8, R9 | 0 Ω: R8(+5 V, fit) / R9(VIN_PROTECTED, DNP) | → SENSOR_SEL |
| Sensor fuse/TVS | F2,D4,C13,C14 | MF-R050, **SMBJ5.0A** (5 V default) | SENSOR_SEL → SENSOR_V+ |
| Sensor terminal | J2 | screw 2P (silk: CHECK V) | SENSOR_V+, GND |
| Test points | TP1–TP5 | VIN_PROTECTED,+5 V,+3V3,GND,SENSOR_V+ | — |
| SW test point | TP6 | **DNP** (bring-up only, EMI/short risk) | SW |
| Power flags | PWR1–6 | ERC drivers on each supply net | — |

## 24→5 V buck (TPS54360) — review items closed

- **UVLO recalculated** (was wrongly noted ~11 V). With TPS54360 Venr ≈ 1.2 V,
  Ihys ≈ 3.4 µA, I1 ≈ 1.2 µA: **R2 = 442 k (top), R3 = 60.4 k (bottom)** →
  Vstart ≈ 10.5 V, Vstop ≈ 9.0 V, so a 12 V supply starts and brown-outs cleanly.
  Confirm against the datasheet thresholds for the exact silicon.
- Catch Schottky D3 (SS5P6, 60 V/5 A), L1 15 µH (Isat ≥ 4 A), boot C4 100 nF,
  RT R4 100 k (~500 kHz), FB R5/R6 (5.0 V), out 2×22 µF + 100 nF.
- **Compensation — calculated from the TPS54360 datasheet (SLVSBB4), Type-II.**
  Inputs: Vref 0.8 V, gm_ea 350 µA/V, gm_ps 12 A/V, fsw 500 kHz, Vout 5 V,
  Cout ≈ 40 µF (2×22 µF ceramic, bias-derated), L1 15 µH, target crossover
  fc ≈ 30 kHz (≈ fsw/16, conservative for phase margin).
  - Rc = (2π·fc·Cout/gm_ps)·(Vout/(Vref·gm_ea))
       = (2π·30k·40µ/12)·(5/(0.8·350µ)) ≈ **11.3 kΩ** → R7 = 11.3 k.
  - Output pole fp = 1/(2π·Cout·(Vout/Iout)) ≈ 1.6 kHz; place comp zero there:
    Cc = 1/(2π·Rc·fp) ≈ **8.2 nF** → C5 = 8.2 nF.
  - HF pole at ~fsw/2: Cc2 = 1/(2π·Rc·250k) ≈ **56 pF** → C6 = 47 pF.
  These are analytic values, NOT arbitrary; **still confirm phase margin (>50°)
  with a bench Bode/transient or a WEBENCH export** before release. (TODO)

## 5→3.3 V SYNC BUCK (was LDO — thermal finding)

LDO replaced with **TPS563201 sync buck** (D-CAP2, few externals: C10 10 µF in,
L2 2.2 µH, C11/C12 out, R11/R12 FB ≈ 3.3 V, R10 EN pull-up). Eliminates the
~0.8–1.3 W LDO dissipation that was risky in a sealed enclosure. AP7361C-33 LDO
retained as a DNP/alt option only.

## Sensor rail (was raw 24 V — destroy-sensor finding)

`SENSOR_V+` now defaults to **+5 V** (R8 fitted) — safe for the common 5 V RS485
mmWave modules. **REQUIREMENT (silkscreen + here): confirm the selected mmWave
sensor supply voltage (SVC-011) before fitting; only ONE source resistor may be
populated.**

### ENFORCED ASSEMBLY VARIANTS (BOM rule)

The assembly house MUST build exactly one of these — not a free mix:

| Variant | R8 (+5 V) | R9 (VIN_PROTECTED) | D4 TVS | Sensor V |
|---------|-----------|--------------------|--------|----------|
| **A (default)** | **FIT 0 Ω** | **DNP** | **SMBJ5.0A** | 5 V |
| **B (wide-input)** | **DNP** | **FIT 0 Ω** | **SMBJ33A** | 12–24 V |

This rule is also recorded in `svc100_power_bom_rev_a.csv` (ASSY-VARIANT rows) so
it cannot be lost. Default = Variant A.

## Front-end transient margin (review item)

Input TVS **SMBJ30A** (600 W): standoff 30 V > 24 V+10 % (26.4 V), clamp ≈ 48 V
< 60 V buck (≈20 % margin). Q1 raised to **100 V** P-FET for reverse/surge
headroom.

**Validated surge envelope (claimed):** indoor, low-exposure secondary only —
i.e., the SMBJ30A's 600 W / ~8 A (8/20 µs) handling, NOT a tested IEC 61000-4-5
class. **This sheet does NOT claim any 61000-4-5 surge level.** For field/outdoor
or long-run-cable installs, Rev B must add an input series choke + higher-energy
TVS (e.g., SMCJ/1.5KE) and/or an 80–100 V buck, then be surge-tested to the
target class. Tracked as a TODO; do not market a surge rating until tested.

## ERC / power-flag strategy

Every supply net carries a `PWR_FLAG`; LDO/buck outputs are `power_out`, power
inputs `power_in`, signals `passive`. Connectivity is by **global labels**
(pin → wire stub → net label). See ERC status in the chat summary.
