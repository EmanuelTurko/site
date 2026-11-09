#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build
cmake --build build -j
exec ./build/site "$@"

