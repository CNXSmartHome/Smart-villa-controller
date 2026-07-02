# AI Task Locks

Agents must record a lock here before editing files for a task, and release it
when done, to avoid concurrent edits across ChatGPT / Claude / Codex / Gemini.

## Format

| Task ID | Agent | Files / Area | State | Acquired (UTC) | Released (UTC) |
|---|---|---|---|---|---|

## Active Locks

_(none)_

## History

| Task ID | Agent | Files / Area | State | Acquired (UTC) | Released (UTC) |
|---|---|---|---|---|---|
| SVC-FW-001 | Claude | (read-only: make doctor/build) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-002 | Claude | hardware/pin_database/svc100_pin_database_rev_a.csv (new) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-003 | Claude | (read-only review: relay.c) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-004 | Claude | (read-only review: dinput.c) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-005 | Claude | (read-only review: rs485.c) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-006 | Claude | (read-only review: modbus_master.c) | Released | 2026-06-29 | 2026-06-29 |
| SVC-FW-007 | Claude | AI_TASK_QUEUE.md, docs/RELEASE_GATES.md, engineering/04_Design_Reviews/DR-0002, .ai/TASK_LOCKS.md | Released | 2026-06-29 | 2026-06-29 |
| DOCS | Claude | docs/00_PROJECT_BIBLE.md, docs/10_SECURITY_STANDARD.md, docs/19_CODING_STANDARD.md, docs/20_PIN_ASSIGNMENT_REV_A.md (new) | Released | 2026-06-29 | 2026-06-29 |
| SVC-013 | Claude | components/logic/* (new), tests/host/test_main.c, tests/host/run.sh | Released | 2026-06-29 | 2026-06-29 |
| SVC-013i | Claude | svc_config.{h,c} (v3), storage.c (migration), components/control/{control_logic.*,control.c,Kconfig,CMakeLists}, tests | Released | 2026-06-29 | 2026-06-29 |
| SVC-012 | Claude | control_logic.c (presence_effective), svc_config v3 fallback fields, tests | Released | 2026-06-29 | 2026-06-29 |
| SVC-010 | Claude | engineering/09_Bench_Test_Procedure.md (draft for CTO/Codex), AI_TASK_QUEUE.md | Released | 2026-06-29 | 2026-06-29 |
| SVC-021 | Claude | components/hal/* (new interface+skeletons), docs/30_HAL_ARCHITECTURE.md, AI_TASK_QUEUE.md | Released | 2026-06-29 | 2026-06-29 |

Notes (SVC-021): interface + plan only; NO existing module refactored (per task
"do not rewrite everything yet"). Existing SVC-100 firmware + host tests untouched.

Notes:
- This sprint made NO firmware source changes and NO GPIO changes. Deliverables
  are documentation, a derived pin-database seed, and review findings.
- Relay safe-off enforcement (`svc_config_sanitize` forcing `relay_safe_on=0`)
  and its host test were already present (team change) and verified, not added
  by this sprint.
