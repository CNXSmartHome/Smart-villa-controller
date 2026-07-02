# 19 — Coding Standard

Firmware coding rules for SVC-100. Subordinate to `docs/00_PROJECT_BIBLE.md`.
Enforced in review by Codex and reflected by the existing codebase.

## 1. Platform & language

- **ESP-IDF 5.x only.** No Arduino, no Arduino types (`String`, etc.).
- C11 for C, C++17 only where a component opts in.
- No new third-party libraries without Owner approval. Managed components are
  declared per-component in `idf_component.yml`.

## 2. Concurrency & timing

- **No `delay()`. No busy-wait. No blocking super-loop.** Each long-lived
  activity is a FreeRTOS task that blocks on a queue/event or uses
  `vTaskDelayUntil`.
- **No `portMAX_DELAY` in any production control path.** Mutex/queue waits must
  be bounded and return an explicit timeout/fault error. Relay control must never
  hang. (The only permitted unbounded wait is an explicit opt-in not used by the
  control task.)
- The control loop is subscribed to the Task Watchdog and must feed it each
  iteration.

## 3. Memory

- Allocate during init only. **No heap allocation on the steady-state hot path.**
- Prefer fixed-size, statically-sized buffers and value-type messages on the
  event bus (no pointers to transient memory across the bus).

## 4. Error handling

- Public functions return `svc_err_t`. Use the `SVC_RETURN_ON_ERR` /
  `SVC_GOTO_ON_ERR` / `SVC_CHECK_ARG` macros — never silently drop an error.
- ISRs never log, never allocate, never block; they only notify tasks.
- On partial init failure, **unwind every resource already acquired** before
  returning (mutexes, drivers, ISR handlers, tasks). See `rs485_open`,
  `dinput_start`, `indicator_start`, `relay_init` for the pattern.

## 5. Module structure

- One responsibility per component: `components/<name>/{include/<name>.h,
  <name>.c, CMakeLists.txt}`.
- Public-vs-private dependencies via `REQUIRES` / `PRIV_REQUIRES`.
- A module may depend only on modules at the same layer or below; notifications
  flow upward via the event bus, commands downward via direct API calls. No
  dependency cycles.
- Centralize all GPIO in `components/board/include/board.h`. Changing a hardware
  revision touches only `board`.

## 6. Documentation

- Document every public function (Doxygen `@brief`/`@param`/`@return`).
- Update the relevant doc when behavior changes (Bible rule).
- Comments explain *why*, not *what*; keep stale comments out (e.g. no "factory
  window" language once a behavior is removed).

## 7. Testability

- Split pure logic from hardware so it can be host-tested without ESP-IDF
  (e.g. `dinput_debounce.h`, `svc_json.c`, `svc_config.c`, `webui_authz.c`,
  `logic` engine core).
- Host tests live in `tests/host/` and run via `tests/host/run.sh` (gcc, no
  ESP-IDF). They build with `-Wall -Wextra`, AddressSanitizer + UBSan on Linux,
  and must finish warning-clean and exit 0.
- Security-relevant logic is tested against the **real** shipped functions, not
  re-implemented mirrors.

## 8. Naming & style

- Types `svc_*_t` / `<module>_*_t`; functions `<module>_<verb>()`; macros and
  constants UPPER_SNAKE; file-scope statics prefixed `s_`.
- A file-scope `static const char *TAG = "<module>";` for logging.
- 4-space indentation, no tabs; keep lines reasonable (~90 cols).
- Booleans are real `bool`; normalize persisted booleans to 0/1.

## 9. Definition of done (code)

Builds for `esp32s3` under ESP-IDF · host tests pass warning-clean · no rule in
§2 violated · errors propagated · resources unwound on failure · public APIs
documented · docs/queue updated · review obtained for security/relay changes.
