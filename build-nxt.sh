#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
JOBS="${JOBS:-4}"
if [[ "${1:-}" == "--tests" ]]; then
    cmake -S tests/nxt -B build/nxt-tests -DCMAKE_BUILD_TYPE=Debug "-DNXT_SANITIZE=${NXT_SANITIZE:-OFF}"
    cmake --build build/nxt-tests --parallel "$JOBS"
    ctest --test-dir build/nxt-tests --output-on-failure
    exit
fi
for font in PressStart2P-Regular.ttf Fipps-Regular.otf Inconsolata-Regular.ttf Montserrat-Regular.ttf NotoSansJP-Regular.ttf; do
    [[ -f "soh/assets/custom/fonts/$font" ]] || { echo "Missing unchanged baseline asset: $font. Apply the overlay to your original source or copy its soh/assets/custom/fonts directory." >&2; exit 1; }
done
# Install the baseline's Linux dependencies first; see docs/BUILDING.md.
cmake -S . -B build/nxt-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/nxt-release --target soh GenerateSohOtr --parallel "$JOBS"
