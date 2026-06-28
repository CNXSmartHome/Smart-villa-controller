# Design Review DR-0001 — Security Hardening

## Scope

Review web API, provisioning, CSRF, relay control, OTA validation, and config sanitization.

## Required Checks

- Auth cannot be disabled by config
- Provisioning gated by explicit mode
- No password in URL
- JSON escaping bounded
- NVS config sanitized
- Relay APIs protected
