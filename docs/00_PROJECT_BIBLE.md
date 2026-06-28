# Smart Villa OS — Project Bible
Version 1.0

## Vision

Create a professional industrial automation platform designed specifically for Pool Villas, Hotels, Smart Homes, and Small Buildings.

The system must operate completely standalone without requiring Home Assistant, Internet access, or cloud connectivity.

Home Assistant integration is optional.

The controller must be reliable enough for commercial installation.

## Product

Primary product:

- SVC-100 Pro — Smart Villa Controller

Future product family:

- SVC-100 Lite
- SVC-100 Pro
- SVC-300
- SVC-500

## Core Philosophy

- Local First
- Offline First
- Industrial Grade
- Fail Safe
- Easy Installer
- OTA Ready
- Open API
- Long-Term Maintainable

## Hardware Platform

Target V1 hardware:

- ESP32-S3-WROOM-1-N16R8
- 12–24VDC input
- RS485 x2
- Relay dry contact x4
- Digital input x8
- Ethernet optional
- Wi-Fi
- USB-C
- RTC
- Buzzer
- RGB/status LED
- Config button
- Reset button
- DIN rail enclosure

## Firmware Platform

- ESP-IDF 5.x
- FreeRTOS
- Modbus RTU
- HTTP Server
- OTA rollback
- NVS config storage
- Optional MQTT
- Optional REST API
- Optional Home Assistant integration

## Safety Rules

- Outputs must enter safe state during boot.
- Relay outputs must not energize before initialization completes.
- Watchdog must be enabled.
- Brownout detection must be enabled.
- OTA rollback must be enabled.
- Fault state must be visible in API/Web UI.
- Never connect hazardous loads during early firmware testing.

## Security Rules

- Authentication is mandatory after provisioning.
- Config must not be allowed to disable authentication for mutating APIs.
- Provisioning must require explicit provisioning mode.
- Provisioning must not be possible from normal LAN mode.
- Passwords must never be accepted in URL query strings.
- Relay control APIs must never be unauthenticated.
- Every mutating browser route must have CSRF protection.
- Every API input must be validated.
- Every JSON string must be safely escaped.
- NVS-loaded fixed arrays must be treated as potentially non-NUL-terminated.

## Coding Rules

- No Arduino.
- No `delay()`.
- No busy waiting.
- No infinite blocking waits in production paths.
- Avoid dynamic allocation after boot.
- No mutable global state without synchronization.
- One module, one responsibility.
- Public functions must be documented.
- Code must be testable.

## Architecture

Hardware Layer  
↓  
Drivers  
↓  
Services  
↓  
Logic Engine  
↓  
API  
↓  
Web UI  
↓  
OTA

## Release Rule

No production firmware may be released unless:

- Unit tests pass.
- Integration tests pass.
- Security review passes.
- Bench testing passes.
- Field testing passes.
- OTA rollback is tested.
- Relay safe-state behavior is verified.

## Product Goal

A controller that can be installed in a commercial Pool Villa and operate continuously with high reliability.
