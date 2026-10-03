#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
if [[ -n "${CXX:-}" ]]; then compiler="$CXX"
elif command -v clang++ >/dev/null 2>&1; then compiler=clang++
else compiler=g++; fi
flags=(-std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests)
"$compiler" "${flags[@]}" src/main.cpp -o build/exchange
"$compiler" "${flags[@]}" tests/test_engine.cpp -o build/test_engine
"$compiler" "${flags[@]}" src/benchmark.cpp -o build/benchmark
printf 'Build complete with %s\n' "$compiler"
