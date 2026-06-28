#!/usr/bin/env bash
set -euo pipefail

echo "Running host tests if available..."
if [ -d "tests/host" ]; then
  find tests/host -maxdepth 2 -type f | sort
else
  echo "No host tests folder."
fi
