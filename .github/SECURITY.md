# Security Policy

## Reporting Security Issues

Do not open public GitHub issues for vulnerabilities.

Report privately to the project owner.

## Security-Critical Areas

- Relay and physical output control
- Authentication and authorization
- Provisioning flow
- CSRF protection
- OTA update and rollback
- NVS config loading and sanitization
- JSON escaping and API input validation

## Minimum Security Rules

- No unauthenticated relay control after provisioning.
- Provisioning must not be possible from normal LAN mode.
- Passwords must never be passed in URL query strings.
- Mutating browser routes must use CSRF protection.
- Stored config must be treated as untrusted.
