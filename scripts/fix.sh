#!/usr/bin/env bash
set -euo pipefail

echo "Smart Villa OS Fix"
echo "=================="

if [ ! -f "CMakeLists.txt" ] || [ ! -d "components" ]; then
  echo "ERROR: Run from repo root."
  echo "Tip:"
  echo "  cd /Users/taweesakaon/Documents/Claude/Projects/svc-100"
  exit 1
fi

if [ -d "components/.github" ]; then
  echo "Moving components/.github -> .github"
  if [ -d ".github" ]; then
    cp -R components/.github/* .github/
    rm -rf components/.github
  else
    mv components/.github .github
  fi
fi

if [ -d "Smart-villa-controller" ]; then
  echo "Removing nested Smart-villa-controller after copying useful files."
  [ -d "Smart-villa-controller/.ai" ] && cp -R Smart-villa-controller/.ai .
  [ -d "Smart-villa-controller/docs" ] && cp -R Smart-villa-controller/docs/* docs/
  [ -d "Smart-villa-controller/hardware" ] && cp -R Smart-villa-controller/hardware .
  [ -d "Smart-villa-controller/manufacturing" ] && cp -R Smart-villa-controller/manufacturing .
  [ -d "Smart-villa-controller/tools" ] && cp -R Smart-villa-controller/tools .
  [ -f "Smart-villa-controller/AI_START_HERE.md" ] && cp Smart-villa-controller/AI_START_HERE.md .
  [ -f "Smart-villa-controller/AGENTS.md" ] && cp Smart-villa-controller/AGENTS.md .
  [ -f "Smart-villa-controller/CLAUDE.md" ] && cp Smart-villa-controller/CLAUDE.md .
  rm -rf Smart-villa-controller
fi

rm -rf build
find . -name ".DS_Store" -delete

touch .gitignore
for line in "build/" ".DS_Store" "*.patch" "sdkconfig" "sdkconfig.old" "managed_components/" "dependencies.lock"; do
  grep -qxF "$line" .gitignore || echo "$line" >> .gitignore
done

echo "Done."
git status --short
