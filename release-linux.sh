#!/usr/bin/env bash
# =============================================================================
# release.sh — Einklang release packager
#
# Produces three release artefacts in the "release/" directory:
#   • EinklangServer_Portable_Linux_<version>.zip
#   • EinklangClient_Portable_Linux_<version>.zip
#   • einklang-server-docker-image_<version>.tar
#
# Usage:  ./release-linux.sh <version>     (e.g. 1.0.0 — normally the git tag without 'v')
# =============================================================================
set -euo pipefail

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build/Release"
RELEASE_DIR="$ROOT_DIR/release"

# ---------------------------------------------------------------------------
# Step 0 — Version (compiled into the binaries and the Docker image)
# ---------------------------------------------------------------------------
VERSION="${1:-}"
VERSION="${VERSION#v}"

if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 <version>" >&2
    exit 1
fi

echo "==> Version: $VERSION"

# ---------------------------------------------------------------------------
# Step 1 — Prepare the release output folder
# ---------------------------------------------------------------------------
echo ""
echo "==> Preparing release directory"

mkdir -p "$RELEASE_DIR"

# Remove any leftover artefacts from a previous run so we don't accidentally
# ship stale files.
rm -f "$RELEASE_DIR"/*.zip "$RELEASE_DIR"/*.tar

# ---------------------------------------------------------------------------
# Step 2 — Configure and build (Release, both targets)
# ---------------------------------------------------------------------------
echo ""
echo "==> Configuring CMake (Release)"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEINKLANG_BUILD_CLIENT=ON \
    -DEINKLANG_VERSION="$VERSION"

echo ""
echo "==> Building EinklangServerApp"
cmake --build "$BUILD_DIR" --target EinklangServerApp --parallel

echo ""
echo "==> Building EinklangClientApp"
cmake --build "$BUILD_DIR" --target EinklangClientApp --parallel

# ---------------------------------------------------------------------------
# Step 3 — Package the server
# ---------------------------------------------------------------------------
SERVER_ZIP="EinklangServer_Portable_Linux_${VERSION}.zip"

echo ""
echo "==> Packaging server → $SERVER_ZIP"

cp "$BUILD_DIR/EinklangServerApp"  "$RELEASE_DIR/EinklangServerApp"
cp "$BUILD_DIR/server-config.yml" "$RELEASE_DIR/server-config.yml"

(cd "$RELEASE_DIR" && zip "$SERVER_ZIP" EinklangServerApp server-config.yml)

rm "$RELEASE_DIR/EinklangServerApp" "$RELEASE_DIR/server-config.yml"

echo "    Created: $RELEASE_DIR/$SERVER_ZIP"

# ---------------------------------------------------------------------------
# Step 4 — Package the client
# ---------------------------------------------------------------------------
CLIENT_ZIP="EinklangClient_Portable_Linux_${VERSION}.zip"

echo ""
echo "==> Packaging client → $CLIENT_ZIP"

cp "$BUILD_DIR/EinklangClientApp"  "$RELEASE_DIR/EinklangClientApp"

(cd "$RELEASE_DIR" && zip "$CLIENT_ZIP" EinklangClientApp)

rm "$RELEASE_DIR/EinklangClientApp"

echo "    Created: $RELEASE_DIR/$CLIENT_ZIP"

# ---------------------------------------------------------------------------
# Step 5 — Build and export the Docker image
# ---------------------------------------------------------------------------
DOCKER_IMAGE="einklang-server"
DOCKER_TAR="einklang-server-docker-image_${VERSION}.tar"

echo ""
echo "==> Building Docker image ($DOCKER_IMAGE)"

docker build --build-arg EINKLANG_VERSION="$VERSION" -t "$DOCKER_IMAGE" "$ROOT_DIR"

echo ""
echo "==> Saving Docker image → $DOCKER_TAR"

docker save "$DOCKER_IMAGE" -o "$RELEASE_DIR/$DOCKER_TAR"

echo "    Created: $RELEASE_DIR/$DOCKER_TAR"

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
echo ""
echo "==> Release $VERSION complete. Artefacts in: $RELEASE_DIR"
ls -lh "$RELEASE_DIR"