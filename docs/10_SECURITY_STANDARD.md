# Security Standard

## Mandatory Rules

- Authentication cannot be disabled by config.
- Relay control must never be unauthenticated.
- Provisioning must be gated by physical/provisioning mode.
- Passwords must not be sent in URL query strings.
- JSON output must be escaped.
- Fixed-size config strings must use bounded length checks.
- Config must be sanitized after CRC validation.
- Mutating browser requests must use CSRF protection.
