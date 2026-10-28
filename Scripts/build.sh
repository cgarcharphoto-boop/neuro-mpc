#!/usr/bin/env bash
# Configure, build (VST3 + AU + Standalone, installed to ~/Library/Audio/Plug-Ins), test, validate AU.
set -euo pipefail
cd "$(dirname "$0")/.."
CONFIG=${1:-Release}
Scripts/fetch-juce.sh
cmake -S . -B build -G Xcode
cmake --build build --config "$CONFIG" --parallel "$(sysctl -n hw.ncpu)" -- -quiet
ctest --test-dir build -C "$CONFIG" --output-on-failure
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumu Nm26 Nrmp | tail -2
