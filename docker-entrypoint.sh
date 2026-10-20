#!/bin/sh
set -e

# Fix ownership of the data directory in case it was mounted from the host
# as root (which overrides the image-layer chown). This runs as root before
# we drop privileges below.
chown -R einklang:einklang /data

# Seed the default config into the data directory on first run so the user
# has something to edit without needing to extract it from the image manually.
if [ ! -f /data/server-config.yml ]; then
    echo "[entrypoint] No server-config.yml found in /data — copying default config."
    cp /app/server-config.default.yml /data/server-config.yml
    chown einklang:einklang /data/server-config.yml
fi

# Drop from root to the non-root 'einklang' user and exec the server.
# gosu cleanly replaces the shell process (PID 1), so SIGTERM is delivered
# directly to the server binary rather than to a shell wrapper.
exec gosu einklang /app/EinklangServerApp "$@"
