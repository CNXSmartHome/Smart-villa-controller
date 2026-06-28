# PCB Design Guide

## PCB Target

- V1 prototype may be 2-layer.
- Production Pro version should target 4-layer for EMI, Ethernet, and RS485 robustness.

## Important Rules

- Keep ESP32 antenna clearance.
- Separate high-current relay paths from logic.
- Add flyback protection where needed.
- Use TVS protection on RS485.
- Add 120Ω termination option on RS485.
- Add clear silkscreen for terminals.
- Review ESP32-S3 strapping/JTAG pins before production.
