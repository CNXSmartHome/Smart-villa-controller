# AI Task Queue — Smart Villa OS / SVC-100

This file is the shared task queue for ChatGPT, Claude, Codex, Gemini, and future engineers.

## Rules

- Every AI must read `AI_START_HERE.md` and `docs/00_PROJECT_BIBLE.md` before working.
- No implementation without a task ID.
- Claude implements.
- Codex reviews.
- ChatGPT acts as CTO / architecture reviewer.
- Gemini handles UI/UX and marketing assets.
- Security and relay-control changes require explicit review.

---

## Status Legend

- `Planned`
- `Ready`
- `In Progress`
- `Review`
- `Bench Test`
- `Done`
- `Blocked`

---

## Sprint 1 — Bring-up and Core Safety

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-001 | Verify repo structure and developer toolkit | ChatGPT | Ready | High | Run `make doctor` |
| SVC-002 | ESP32-S3 board bring-up review | Claude | Planned | High | GPIO, boot, partitions |
| SVC-003 | Relay driver safety review | Codex | Planned | High | Safe state, bounded waits |
| SVC-004 | Digital input debounce review | Claude | Planned | Medium | Door/keycard/dry contact |
| SVC-005 | RS485 driver review | Codex | Planned | High | Timeout, lock, cleanup |
| SVC-006 | Modbus master validation review | Codex | Planned | High | Frame length, CRC, echo |
| SVC-007 | Presence sensor integration plan | ChatGPT | Planned | High | MTD062/MTD262 |
| SVC-008 | Web API auth and CSRF review | Codex | Planned | Critical | Relay API protection |
| SVC-009 | OTA health-check validation | Claude | Planned | High | No fixed-delay validation |
| SVC-010 | Bench test procedure | ChatGPT | Planned | High | Non-hazardous load only |

---

## Sprint 2 — mmWave and Logic

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-011 | mmWave RS485 register mapping | ChatGPT | Planned | High | Need final register map |
| SVC-012 | Dry contact fallback mode | Claude | Planned | High | S1/S2 input |
| SVC-013 | Logic engine v0.1 | Claude | Planned | High | IF/FOR/THEN |
| SVC-014 | Web UI settings page | Gemini | Planned | Medium | Installer-focused |
| SVC-015 | MQTT optional design | ChatGPT | Planned | Low | Not required for standalone |

---

## Sprint 3 — Hardware / PCB

| ID | Task | Owner | Status | Priority | Notes |
|---|---|---|---|---|---|
| SVC-016 | Hardware block diagram Rev A | ChatGPT | Planned | High | Power, MCU, IO |
| SVC-017 | BOM Rev A | ChatGPT | Planned | High | ESP32-S3, RS485, relay |
| SVC-018 | PCB schematic checklist | ChatGPT | Planned | High | Protection and strapping |
| SVC-019 | KiCad project structure | Claude | Planned | Medium | hardware/kicad |
| SVC-020 | Manufacturing checklist review | Codex | Planned | Medium | DRC/ERC/test fixture |

---

## Current Blockers

| Blocker | Owner | Resolution |
|---|---|---|
| Real mmWave sensor not yet received | Owner | Continue docs, firmware review, bench prep |
| ESP-IDF local build status unknown | Owner | Run `make doctor` and `make build` |
| GitHub Actions status not verified | ChatGPT | Check after `.github` commit |

---

## Next Command for Owner

```bash
make doctor
git status
```

Send the output to ChatGPT for CTO review.
