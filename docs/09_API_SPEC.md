# API Specification

## Core APIs

- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `GET /api/io`
- `POST /api/io`
- `POST /api/ota`
- `POST /api/reboot`
- `POST /api/provision`

## Security

- All mutating routes require authentication after provisioning.
- CSRF protection required for browser-originated mutating requests.
- Provisioning allowed only in explicit provisioning mode.
- No password in URL query string.
