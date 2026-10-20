# AppRun hook for the Einklang AppImage (sourced by linuxdeploy's AppRun).

# The AppImage is read-only, so keep the client configuration in the user's
# config directory and seed it with the defaults on first start.
einklang_config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/einklang"
if [ ! -f "$einklang_config_dir/client-config.yml" ]; then
    mkdir -p "$einklang_config_dir"
    cp "$APPDIR/usr/share/einklang/client-config.yml" "$einklang_config_dir/client-config.yml"
    chmod u+w "$einklang_config_dir/client-config.yml"
fi
export EINKLANG_CLIENT_CONFIG="$einklang_config_dir/client-config.yml"

# libwebkitgtk was patched to look for its helper processes relative to
# "$APPDIR/usr" (see build-appimage.sh).
cd "$APPDIR/usr" || exit 1

# WebKit's bubblewrap sandbox cannot see into the AppImage mount and would
# require bwrap on the host. The client only renders its own embedded UI.
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
