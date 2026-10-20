#!/usr/bin/env bash
# =============================================================================
# build-appimage.sh — Package EinklangClientApp as an AppImage
#
# Bundles the client together with GTK 4, libadwaita and WebKitGTK 6.0 so it
# runs on distributions that do not ship those libraries.  Meant to run on the
# oldest supported build host (Ubuntu 24.04 in CI); the resulting AppImage
# requires at least that host's glibc.
#
# Usage:  packaging/linux/build-appimage.sh <build-dir> <version> [output-dir]
#
# WebKitGTK spawns helper processes (WebKitWebProcess, WebKitNetworkProcess)
# from a directory compiled into libwebkitgtk.  That absolute path is rewritten
# to an equally long relative one ("/usr/..." -> "././/..."), and the AppRun
# hook changes into "$APPDIR/usr" so it resolves inside the AppImage.
# =============================================================================
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <build-dir> <version> [output-dir]" >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PKG_DIR="$ROOT_DIR/packaging/linux"
BUILD_DIR="$(cd "$1" && pwd)"
VERSION="$2"
OUT_DIR="$(mkdir -p "${3:-$ROOT_DIR/release}" && cd "${3:-$ROOT_DIR/release}" && pwd)"

TOOLS_DIR="$BUILD_DIR/appimage-tools"
APPDIR="$BUILD_DIR/AppDir"

# Pinned tool versions — bump deliberately.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
GTK_PLUGIN_URL="https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/7a3fbc31a9e5075073ff8790f26effbac5f84453/linuxdeploy-plugin-gtk.sh"

# CI runners have no FUSE; let the tool AppImages extract themselves instead.
export APPIMAGE_EXTRACT_AND_RUN=1

# ---------------------------------------------------------------------------
# Step 1 — Fetch linuxdeploy and the GTK plugin
# ---------------------------------------------------------------------------
echo "==> Fetching linuxdeploy"
mkdir -p "$TOOLS_DIR"
[[ -x "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" ]] || \
    curl -fsSL -o "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY_URL"
[[ -x "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh" ]] || \
    curl -fsSL -o "$TOOLS_DIR/linuxdeploy-plugin-gtk.sh" "$GTK_PLUGIN_URL"
chmod +x "$TOOLS_DIR"/*
export PATH="$TOOLS_DIR:$PATH"

# ---------------------------------------------------------------------------
# Step 2 — Lay out the AppDir
# ---------------------------------------------------------------------------
echo ""
echo "==> Preparing AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BUILD_DIR/EinklangClientApp"           "$APPDIR/usr/bin/EinklangClientApp"
install -Dm644 "$ROOT_DIR/src/client/client-config.yml"  "$APPDIR/usr/share/einklang/client-config.yml"
install -Dm644 "$PKG_DIR/einklang.desktop"               "$APPDIR/usr/share/applications/einklang.desktop"
install -Dm644 "$PKG_DIR/einklang.png"                   "$APPDIR/usr/share/icons/hicolor/64x64/apps/einklang.png"
install -Dm644 "$PKG_DIR/apprun-hook.sh"                 "$APPDIR/apprun-hooks/einklang.sh"

# Locate the WebKitGTK helper directory of the build host.
WEBKIT_PROCESS="$(find /usr/lib /usr/libexec -name WebKitWebProcess -path '*webkitgtk-6.0*' -print -quit 2>/dev/null || true)"
if [[ -z "$WEBKIT_PROCESS" ]]; then
    echo "ERROR: WebKitWebProcess (webkitgtk-6.0) not found on this system" >&2
    exit 1
fi
WEBKIT_DIR="$(dirname "$WEBKIT_PROCESS")"
if [[ "$WEBKIT_DIR" != /usr/* ]]; then
    echo "ERROR: unexpected WebKitGTK helper directory: $WEBKIT_DIR" >&2
    exit 1
fi
echo "    WebKitGTK helpers: $WEBKIT_DIR"

mkdir -p "$APPDIR/$(dirname "$WEBKIT_DIR")"
cp -a "$WEBKIT_DIR" "$APPDIR/$WEBKIT_DIR"
# MiniBrowser is a demo app some distributions ship alongside the helpers.
rm -f "$APPDIR/$WEBKIT_DIR/MiniBrowser"

# ---------------------------------------------------------------------------
# Step 3 — Deploy shared libraries (client, WebKit helpers, GTK runtime)
# ---------------------------------------------------------------------------
echo ""
echo "==> Deploying dependencies"
DEPLOY_GTK_VERSION=4 linuxdeploy-x86_64.AppImage \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/EinklangClientApp" \
    --deploy-deps-only "$APPDIR/$WEBKIT_DIR" \
    --desktop-file "$APPDIR/usr/share/applications/einklang.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/64x64/apps/einklang.png" \
    --plugin gtk

# ---------------------------------------------------------------------------
# Step 4 — Make libwebkitgtk look for its helpers inside the AppImage
# ---------------------------------------------------------------------------
echo ""
echo "==> Relocating WebKitGTK helper path"
WEBKIT_LIB="$(find "$APPDIR/usr/lib" -name 'libwebkitgtk-6.0.so*' -type f -print -quit)"
if [[ -z "$WEBKIT_LIB" ]]; then
    echo "ERROR: libwebkitgtk-6.0 was not deployed into the AppDir" >&2
    exit 1
fi
python3 - "$WEBKIT_LIB" "$WEBKIT_DIR" <<'EOF'
import sys
path, old = sys.argv[1], sys.argv[2].encode()
new = b"././" + old[len(b"/usr"):]  # same length, relative to $APPDIR/usr
data = open(path, "rb").read()
count = data.count(old)
if count == 0:
    sys.exit(f"ERROR: {old.decode()} not found in {path}")
open(path, "wb").write(data.replace(old, new))
print(f"    Patched {count} occurrence(s) in {path}")
EOF

# ---------------------------------------------------------------------------
# Step 5 — Build the AppImage
# ---------------------------------------------------------------------------
echo ""
echo "==> Building AppImage"
APPIMAGE_NAME="Einklang-${VERSION}-x86_64.AppImage"
(
    cd "$OUT_DIR"
    rm -f "$APPIMAGE_NAME"
    LDAI_OUTPUT="$APPIMAGE_NAME" linuxdeploy-x86_64.AppImage --appdir "$APPDIR" --output appimage
)

echo ""
echo "==> Created: $OUT_DIR/$APPIMAGE_NAME"
