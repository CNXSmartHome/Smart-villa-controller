#!/usr/bin/env bash
set -euo pipefail

if ! command -v idf.py >/dev/null 2>&1; then
  echo "ERROR: idf.py not found. Open ESP-IDF terminal or source export.sh first."
  exit 1
fi

idf.py build
