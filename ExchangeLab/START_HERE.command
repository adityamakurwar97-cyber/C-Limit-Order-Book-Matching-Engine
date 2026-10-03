#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1; then
  echo 'Python 3 is required. Install Python 3, then run this launcher again.'
  exit 1
fi
if ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
  echo 'A C++ compiler is required. On Mac, run: xcode-select --install'
  exit 1
fi
bash scripts/build.sh
./build/test_engine
python3 server.py "$@"
