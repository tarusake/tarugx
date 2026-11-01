#!/bin/sh
# Snap Firefox needs a user scope when launched from the dedicated Xfce unit.
set -eu
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
export LIBGL_ALWAYS_SOFTWARE=1
export MOZ_ENABLE_WAYLAND=0
exec systemd-run --user --scope --quiet firefox \
    --profile "$HOME/snap/firefox/common/tarugx-profile" \
    "${1:-https://abehiroshi.la.coocan.jp/}"
