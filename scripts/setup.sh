#!/usr/bin/env bash
set -euo pipefail

echo "Smart Villa OS macOS setup helper"
echo "================================="

if [[ "$(uname)" != "Darwin" ]]; then
  echo "ERROR: macOS only."
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew missing. Install from https://brew.sh first."
  exit 1
fi

brew install git cmake ninja python tree || true

echo "Recommended manual installs:"
echo "- VS Code"
echo "- KiCad"
echo "- GitHub Desktop"
echo "- ESP-IDF extension in VS Code"
echo "- Claude Code"
