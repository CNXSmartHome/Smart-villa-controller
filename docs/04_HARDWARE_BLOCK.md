# Hardware Block Diagram

```text
24VDC IN
  |
Protection
  |
Buck 5V
  |
LDO 3.3V
  |
ESP32-S3
  |-- RS485 #1 --> mmWave
  |-- RS485 #2 --> Energy meter / spare
  |-- Relay x4 --> Light / AC / Fan / Spare
  |-- DI x8 --> Door / Leak / Smoke / Keycard
  |-- Ethernet
  |-- Wi-Fi
  |-- USB-C
  |-- RTC
  |-- Buzzer / LED
```
