#!/usr/bin/env bash
# Fetches the pinned JUCE release into ./JUCE.
set -euo pipefail
cd "$(dirname "$0")/.."
TAG=9.0.4
[ -d JUCE ] && { echo "JUCE already present"; exit 0; }
git -c advice.detachedHead=false clone --depth 1 --branch "$TAG" https://github.com/juce-framework/JUCE.git JUCE
