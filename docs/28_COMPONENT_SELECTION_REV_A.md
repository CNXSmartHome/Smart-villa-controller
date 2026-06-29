# SVC-100 Pro — Component Selection Rev A

## Purpose

This document selects practical Rev A components for SVC-100 Pro so schematic design can move from placeholders to real circuits.

Rev A goal: buildable engineering prototype with production-oriented parts where reasonable.

## Selection Principles

1. Prefer parts that are easy to source from LCSC / Mouser / Digi-Key / JLCPCB.
2. Prefer 3.3V logic parts for ESP32-S3 compatibility.
3. Use industrial-style protection for field wiring.
4. Use second-source options where possible.
5. Avoid obscure parts unless they solve a clear problem.
6. Rev A can use pragmatic parts, but the schematic should allow a stronger Rev B.

---

# Recommended Rev A Component Choices

## 1. MCU

### Primary

**ESP32-S3-WROOM-1-N16R8**

Reason:
- ESP-IDF support
- Native USB
- Wi-Fi
- 16MB Flash
- 8MB PSRAM
- Enough memory for Web UI, OTA, and local logic

### Notes

- Must follow Espressif reference design.
- Antenna keepout must be respected.
- GPIO boot strapping must be reviewed before PCB release.

### Status

**Selected**

---

## 2. Power Input Protection

### Fuse / PTC

Primary:
- Resettable PTC fuse, 24V-compatible, hold current selected after power budget

Candidate:
- Bourns MF-R series
- Littelfuse PolySwitch series

Reason:
- Protects against wiring fault
- Easy to source
- Good for field device prototype

### Reverse Polarity

Rev A recommendation:
- Series Schottky diode for simplicity OR PMOS ideal diode for lower voltage drop

Preferred production:
- PMOS reverse polarity protection

Reason:
- 24V input gives enough headroom
- PMOS is better for efficiency and thermal behavior

### TVS

Primary:
- SMBJ33A or SMBJ36A class TVS for 24V field input

Reason:
- 24V systems can see transients
- TVS should be selected based on maximum normal input voltage and regulator rating

### Status

**Open — finalize after exact VIN range and buck regulator selection**

---

## 3. 24V to 5V Buck Regulator

### Rev A Practical Option

**MP1584EN-based buck design/module style reference**

Reason:
- Common
- Cheap
- Wide input
- Enough current for prototype
- Easy to test

### Production-Oriented Alternatives

- TI TPS54331
- TI LM2596 family
- MPS MP2451 / MP2307 family
- Recom / Murata DC-DC module for higher reliability

### Rev A Recommendation

Use a proven buck IC/module footprint strategy:

- For first board: use onboard buck footprint if confidence is high
- Or provide footprint for compact DC-DC module for bring-up safety

### Required Specs

- Input: at least 30V rating, preferably 36V+
- Output: 5V
- Current: 1.5A minimum, 2A preferred
- Thermal margin required

### Status

**Candidate — needs final electrical sizing**

---

## 4. 5V to 3.3V Regulator

### Primary

**AMS1117-3.3 is NOT recommended for production**

It can work for lab prototypes, but ESP32 Wi-Fi current spikes and heat dissipation can be problematic.

### Recommended

- AP2112K-3.3 for low/moderate load
- TLV75533 / TLV76733 class LDO
- Buck 5V to 3.3V if thermal/current budget requires

### Rev A Recommendation

Use a robust 3.3V regulator rated at **700mA minimum**, preferably 1A.

### Status

**Open — choose after power budget**

---

## 5. RS485 Transceiver

### Primary Rev A

**MAX3485 / SP3485**

Reason:
- 3.3V logic
- Common
- Simple
- Good ESP32 compatibility

### Better Industrial Alternatives

- TI THVD1450
- TI SN65HVD1781
- Analog Devices ADM3485

### Rev A Recommendation

Use **SP3485 or MAX3485 footprint**, with schematic allowing compatible replacements.

### Required Protection

- TVS diode array on A/B
- Optional 120Ω termination jumper
- Bias resistor option
- Clear A/B silkscreen

### Status

**Selected for Rev A: SP3485/MAX3485 class**

---

## 6. RS485 Protection

### Candidate TVS

- SM712 for RS485 A/B
- Low-capacitance bidirectional ESD array for differential bus

### Recommendation

Use **SM712** or equivalent RS485 protection diode.

Reason:
- Common RS485 protection part
- Designed for asymmetric RS485 common-mode range

### Status

**Selected candidate**

---

## 7. Relay

### Rev A Practical

- Hongfa / Omron / Panasonic 5V relay
- Contact terminals COM/NO/NC

### Preferred Production

- Omron G5Q / G5LE class
- Panasonic relay equivalent
- Finder relay equivalent

### Avoid

- Unknown relay with unclear contact rating for field use

### Rev A Recommendation

Use a known relay footprint that can accept at least one high-quality relay family.

If cost-sensitive prototype requires Songle/Hongfa, document it as prototype-only unless rating is validated.

### Coil Voltage

- 5V coil preferred if +5V rail has enough current
- Consider 12V relay if thermal/current budget is better

### Driver

- NPN transistor or logic MOSFET per relay
- Flyback diode
- Optional relay status LED

### Status

**Open — choose exact relay footprint before schematic drawing**

---

## 8. Digital Input Front-End

### Rev A Recommendation

Protected dry contact input:

- Pull-up to 3.3V
- Series resistor
- RC filter footprint
- ESD diode footprint
- Optional optocoupler footprint if board space allows

### Production Pro Recommendation

Use optocoupler or digital isolator front-end for field wiring robustness.

### Notes

Dry contact inputs may connect to long cables, so ESD and debounce matter.

### Status

**Selected topology, exact parts open**

---

## 9. USB-C

### Required

- USB-C receptacle
- CC resistors for device mode
- USB D+/D- to ESP32-S3 native USB
- ESD protection close to connector

### Candidate

- USBLC6-2SC6 or equivalent USB ESD protection

### Status

**Selected topology, connector part open**

---

## 10. RTC

### Candidate

**DS3231**

Reason:
- Accurate
- Common
- I2C
- Battery-backed options available

### Alternative

- PCF8563 for lower cost

### Rev A Recommendation

Use DS3231 if schedule/time reliability matters. If not required in Rev A, mark RTC DNP but keep footprint.

### Status

**Candidate: DS3231**

---

## 11. Buzzer

### Recommendation

- 3.3V or 5V active buzzer with transistor driver
- GPIO PWM optional if passive buzzer selected

### Rev A

Use active buzzer for simpler firmware.

### Status

**Selected topology**

---

## 12. LEDs

### Minimum

- Power LED
- System LED
- Fault LED
- Network LED

### Optional

- Relay status LED per channel
- RS485 activity LED per port

### Recommendation

Use simple single-color LEDs for Rev A. RGB can be added later.

### Status

**Selected**

---

## 13. Terminal Blocks

### Recommendation

Use pluggable screw terminal blocks.

Suggested pitch:
- 3.5mm or 3.81mm for low-voltage signals
- 5.08mm for relay/contact outputs if needed

Brands:
- Phoenix Contact
- Degson
- KF/clone for prototype only

### Status

**Open — choose based on enclosure and relay spacing**

---

## 14. Ethernet

### Options

#### LAN8720
Pros:
- ESP-IDF support
- Common RMII PHY
- Lower BOM

Cons:
- Requires RMII pins and clock care
- More sensitive layout

#### W5500
Pros:
- SPI Ethernet
- Easier conceptual integration
- Good module availability

Cons:
- More BOM cost
- Uses SPI bus
- Not native MAC

### Rev A Recommendation

Keep Ethernet DNP/optional until after ESP32 pin and layout review.

If Ethernet is required in Rev A, choose **W5500** for easier prototype bring-up.

If production cost and integration are priority, choose **LAN8720** for Rev B after pin review.

### Status

**Open**

---

# Rev A Recommended BOM Summary

| Block | Selected / Candidate | Status |
|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | Selected |
| RS485 | SP3485 / MAX3485 | Selected candidate |
| RS485 TVS | SM712 equivalent | Selected candidate |
| Relay | Omron/Panasonic/Finder/Hongfa footprint | Open |
| 24V→5V | MP1584/TPS54331 class | Open |
| 5V→3.3V | 700mA–1A regulator | Open |
| USB ESD | USBLC6 equivalent | Candidate |
| RTC | DS3231 | Candidate |
| Terminal | Pluggable screw terminal | Open |
| DI protection | resistor + RC + ESD, opto optional | Selected topology |

---

# Decisions Needed Before Schematic Drawing

## Must Decide

1. Exact buck regulator or module footprint
2. Exact 3.3V regulator
3. Exact relay footprint
4. Terminal pitch
5. Ethernet Rev A: populated, DNP, or excluded
6. DI input: protected resistor topology vs optocoupler

## CTO Recommendation

For fastest Rev A:

- ESP32-S3-WROOM-1-N16R8
- MP1584/TPS54331-class 5V buck
- 1A 3.3V regulator
- SP3485/MAX3485 RS485
- SM712 RS485 TVS
- Relay x4 with 5V coil
- Protected resistor/RC/ESD DI inputs
- USB-C with ESD
- RTC footprint DNP allowed
- Ethernet DNP for Rev A unless required

This keeps Rev A buildable while preserving upgrade paths for Rev B.
