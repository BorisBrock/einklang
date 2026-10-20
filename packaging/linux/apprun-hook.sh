# AppRun hook for the Einklang AppImage (sourced by linuxdeploy's AppRun).

# The AppImage is read-only, so keep the client configuration in the user's
# config directory. The client creates it on first start.
export EINKLANG_CLIENT_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/einklang/client-config.yml"

# libwebkitgtk was patched to look for its helper processes relative to
# "$APPDIR/usr" (see build-appimage.sh).
cd "$APPDIR/usr" || exit 1

# WebKit's bubblewrap sandbox cannot see into the AppImage mount and would
# require bwrap on the host. The client only renders its own embedded UI.
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
