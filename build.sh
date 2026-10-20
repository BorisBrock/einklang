#!/usr/bin/env bash
# =============================================================================
# build.sh — Build EinklangServerApp and EinklangClientApp
#
# Configures (if needed) and builds both targets into build/<variant>/,
# where <variant> is Debug or Release.
#
# Usage:  ./build.sh [Debug|Release]     (default: Release)
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VARIANT="${1:-Release}"

case "$VARIANT" in
    Debug|Release) ;;
    *)
        echo "Usage: $0 [Debug|Release]" >&2
        exit 1
        ;;
esac

BUILD_DIR="$ROOT_DIR/build/$VARIANT"

echo "==> Configuring CMake ($VARIANT)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$VARIANT" \
    -DEINKLANG_BUILD_CLIENT=ON

echo ""
echo "==> Building EinklangServerApp and EinklangClientApp"
cmake --build "$BUILD_DIR" --target EinklangServerApp EinklangClientApp --parallel

echo ""
echo "==> Build complete. Binaries in: $BUILD_DIR"
