#!/usr/bin/env bash
set -euo pipefail

name="${1:-}"
if [ -z "$name" ]; then
  echo "Usage: ./scripts/new_driver.sh driver_name"
  exit 1
fi

./scripts/new_component.sh "$name"
echo "Driver checklist:"
echo "- bounded waits only"
echo "- cleanup on init failure"
echo "- no business logic"
echo "- deterministic hardware state"
