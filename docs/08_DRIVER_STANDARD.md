# Driver Standard

## Rules

- Drivers must not block forever.
- All locks must use bounded waits.
- Initialization failure must cleanup partial resources.
- Public driver APIs must return explicit error codes.
- Drivers must not own business logic.
- Hardware state must be deterministic after init.
