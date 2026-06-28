#!/usr/bin/env bash
set -euo pipefail

version="${1:-}"
if [ -z "$version" ]; then
  echo "Usage: ./scripts/release.sh v0.1.0"
  exit 1
fi

./scripts/doctor.sh
./scripts/build.sh

git tag -a "$version" -m "Release $version"
echo "Created tag $version. Push with:"
echo "git push origin $version"
