#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
# Run as the desktop user after the dedicated Xorg server is ready.
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export DISPLAY=:1
export XAUTHORITY="$host_dir/Xauthority"
export XDG_CONFIG_HOME="$host_dir/desktop-config"
export XDG_CACHE_HOME="$host_dir/desktop-cache"
export XDG_CURRENT_DESKTOP=XFCE
export DESKTOP_SESSION=xfce
config_dir="$XDG_CONFIG_HOME/xfce4/xfconf/xfce-perchannel-xml"
mkdir -p "$config_dir" "$XDG_CACHE_HOME"
mkdir -p "$XDG_CONFIG_HOME/autostart"
# Suppress upgrade popups in this test session only; no system update changes.
cat > "$XDG_CONFIG_HOME/autostart/update-notifier.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Update Notifier
Hidden=true
EOF
if [ ! -e "$config_dir/xfwm4.xml" ]; then
    cat > "$config_dir/xfwm4.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<channel name="xfwm4" version="1.0">
  <property name="general" type="empty">
    <property name="use_compositing" type="bool" value="false"/>
    <property name="workspace_count" type="int" value="1"/>
  </property>
</channel>
EOF
fi
if [ ! -e "$config_dir/xfce4-panel.xml" ] && [ -r /etc/xdg/xfce4/panel/default.xml ]; then
    cp /etc/xdg/xfce4/panel/default.xml "$config_dir/xfce4-panel.xml"
fi
exec dbus-run-session -- xfce4-session
