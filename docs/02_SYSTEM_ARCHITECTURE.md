# System Architecture

## Layered Model

1. Board Support
2. Drivers
3. Services
4. Logic Engine
5. API
6. Web UI
7. OTA and Maintenance

## Data Flow

Sensor input → Driver → Event bus → Logic engine → Relay service → Output

## Fault Flow

Fault detected → Fault manager → Safe state → API/Web status → Log
