# Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| Relay unsafe on boot | High | Safe-state boot and tests |
| Unauthenticated relay control | High | Mandatory auth and CSRF |
| OTA bad image | High | Health-check validation and rollback |
| mmWave false positives | Medium | Tuning and field testing |
| RS485 noise | Medium | TVS, termination, shield guidance |
| ESP32 strapping pin error | High | Hardware release gate |
