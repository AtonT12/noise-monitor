#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/noise-monitor-version-c-tests"
mkdir -p "$BUILD_DIR"

CXX="${CXX:-c++}"
COMMON_FLAGS=(-std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT_DIR/include")

"$CXX" "${COMMON_FLAGS[@]}" \
  "$ROOT_DIR/src/alert_controller.cpp" \
  "$ROOT_DIR/tests/test_alert_controller.cpp" \
  -o "$BUILD_DIR/test_alert_controller"

"$CXX" "${COMMON_FLAGS[@]}" \
  "$ROOT_DIR/src/wav_parser.cpp" \
  "$ROOT_DIR/tests/test_wav_parser.cpp" \
  -o "$BUILD_DIR/test_wav_parser"

"$CXX" "${COMMON_FLAGS[@]}" \
  "$ROOT_DIR/src/calibration.cpp" \
  "$ROOT_DIR/tests/test_calibration.cpp" \
  -o "$BUILD_DIR/test_calibration"

"$BUILD_DIR/test_alert_controller"
"$BUILD_DIR/test_wav_parser"
"$BUILD_DIR/test_calibration"
