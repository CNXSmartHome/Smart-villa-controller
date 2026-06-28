# AI Start Here

This file defines how AI agents and developers must work on the Smart Villa Controller project.

## Mandatory Reading Order

Before doing any task, read:

1. `docs/00_PROJECT_BIBLE.md`
2. The relevant architecture/spec document
3. The assigned task or issue

If there is a conflict, `docs/00_PROJECT_BIBLE.md` wins.

## Role Rules

### Claude / Firmware Engineer
- Implement only assigned tasks.
- Use ESP-IDF only.
- Do not use Arduino.
- Do not redesign architecture without approval.
- Do not add libraries without approval.
- Update documentation when code changes.
- Produce production-oriented code, not demo-only code.

### Codex / Principal Reviewer
- Review for security, reliability, race conditions, memory safety, and production risks.
- Do not add new features unless asked.
- Return concrete findings and patches.
- Mark whether code is safe for bench testing.

### Gemini / UI and Marketing
- Create UI mockups, diagrams, visual assets, and marketing material.
- Must follow the product vision and terminology in the Project Bible.

## Engineering Rules

- No relay control without authentication after provisioning.
- No provisioning from normal LAN mode.
- No password in URL.
- All JSON output must be safely escaped.
- No `delay()`.
- No blocking forever in production control paths.
- OTA image must be validated by health checks, not fixed delay.
