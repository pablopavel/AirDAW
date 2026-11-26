#!/bin/sh
# Fetches the third-party sources when you have the project without git submodules (e.g. from the zip).
set -e
mkdir -p third_party
[ -d third_party/JUCE ] || git clone --depth 1 https://github.com/juce-framework/JUCE third_party/JUCE
[ -d third_party/airwin2rack ] || git clone --depth 1 https://github.com/baconpaul/airwin2rack third_party/airwin2rack
echo "Done. Build: cmake -B build -G Ninja && cmake --build build --target engine_tests"
