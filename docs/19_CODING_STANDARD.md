# Coding Standard

## Language and Framework

- ESP-IDF only.
- No Arduino.
- C/C++ allowed.

## Runtime Rules

- No `delay()`.
- No busy waiting.
- No unbounded mutex waits in production paths.
- Avoid dynamic allocation after boot.
- Use FreeRTOS primitives correctly.

## Code Rules

- One module, one responsibility.
- Keep functions short.
- Document public APIs.
- Validate all inputs.
- Return explicit error codes.
- Use bounded string functions.
- Treat stored config as untrusted.
