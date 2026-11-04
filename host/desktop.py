#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Start/stop the dedicated TaruGX Xorg + Xfce session (no boot enablement)."""
import argparse
import os
from pathlib import Path
import pwd
import secrets
import subprocess
import time


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def active(unit):
    return subprocess.run(["systemctl", "is-active", "--quiet", unit]).returncode == 0


def stop():
    for unit in ("tarugx-terminal", "tarugx-desktop", "tarugx-xorg"):
        if active(unit):
            run(["systemctl", "stop", unit])


def make_primary(user, host_dir):
    env = ["runuser", "-u", user, "--", "env", "DISPLAY=:1",
           f"XAUTHORITY={host_dir / 'Xauthority'}"]
    output = subprocess.check_output([*env, "xrandr", "--current"], text=True)
    connected = [line.split()[0] for line in output.splitlines()
                 if len(line.split()) > 1 and line.split()[1] == "connected"]
    # The fixed-mode TaruGX DDX owns one immutable screen and intentionally
    # does not expose RandR output objects. There is nothing to select in that
    # case. The modesetting fallback still has exactly one named output.
    if not connected and "1920 x 1080" in output:
        return
    if len(connected) != 1:
        raise RuntimeError("expected exactly one connected output on the dedicated TaruGX Xorg")
    run([*env, "xrandr", "--output", connected[0], "--primary"])


def wait_for_xorg(user, env_args, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            ready = subprocess.run(["runuser", "-u", user, "--", "env", *env_args,
                                    "xrandr", "--current"], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL,
                                   timeout=min(5, max(0.1, deadline - time.monotonic())))
            if ready.returncode == 0:
                break
        except subprocess.TimeoutExpired:
            # Cold boot can spend several seconds loading software GL.
            # A probe timeout is not a failed X server: retry to the deadline.
            pass
        time.sleep(0.2)
    else:
        raise RuntimeError("Xorg did not become ready before the deadline; inspect Xorg.1.log")


def start(user, terminal):
    account = pwd.getpwnam(user)
    if account.pw_uid == 0:
        raise ValueError("desktop must run as a non-root user")
    host_dir = Path(__file__).resolve().parent
    runtime = Path(f"/run/user/{account.pw_uid}")
    if not runtime.is_dir():
        raise ValueError("log in as the desktop user first (XDG runtime directory is absent)")
    driver = Path("/sys/bus/pci/devices/0000:01:00.0/driver")
    if not driver.exists() or driver.resolve().name != "tarugx":
        raise ValueError("load tarugx for 0000:01:00.0 before starting Xorg")
    if active("tarugx-xorg") or Path("/tmp/.X11-unix/X1").exists():
        raise ValueError("display :1 is already running; stop it explicitly before starting")
    if active("tarugx-modetest"):
        run(["systemctl", "stop", "tarugx-modetest"])
    authority = host_dir / "Xauthority"
    fd = os.open(authority, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.fchmod(fd, 0o600)
        os.fchown(fd, account.pw_uid, account.pw_gid)
    finally:
        os.close(fd)
    run(["runuser", "-u", user, "--", "xauth", "-f", str(authority)],
        input=f"add :1 MIT-MAGIC-COOKIE-1 {secrets.token_hex(16)}\n", text=True)
    try:
        run(["systemd-run", "--unit=tarugx-xorg", "--collect", "/usr/lib/xorg/Xorg",
             ":1", "vt8", "-config", str(host_dir / "xorg-tarugx.conf"),
             "-auth", str(authority), "-nolisten", "tcp", "-noreset",
             "-logfile", str(host_dir / "Xorg.1.log")])
        env_args = [f"DISPLAY=:1", f"XAUTHORITY={authority}"]
        wait_for_xorg(user, env_args)
        make_primary(user, host_dir)
        common = ["--collect", f"--uid={user}", f"--working-directory={account.pw_dir}",
                  f"--setenv=HOME={account.pw_dir}", f"--setenv=XDG_RUNTIME_DIR={runtime}"]
        run(["systemd-run", "--unit=tarugx-desktop", *common,
             "/bin/sh", str(host_dir / "start-xfce.sh")])
        if terminal:
            # Xorg readiness alone does not mean Xfce has finished startup.
            for _ in range(60):
                wm = subprocess.run(
                    ["runuser", "-u", user, "--", "env", *env_args,
                     "xprop", "-root", "_NET_SUPPORTING_WM_CHECK"],
                    capture_output=True, text=True, timeout=5)
                if wm.returncode == 0 and "window id # 0x" in wm.stdout:
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError("Xfce window manager did not become ready")
            run(["systemd-run", "--unit=tarugx-terminal", *common,
                 "--setenv=DISPLAY=:1", f"--setenv=XAUTHORITY={authority}",
                 f"--setenv=XDG_CONFIG_HOME={host_dir / 'desktop-config'}",
                 "/usr/bin/xfce4-terminal", "--disable-server", "--title=TaruGX/100",
                 "--geometry=60x16+45+100"])
    except Exception:
        stop()
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "stop"))
    parser.add_argument("--user", help="non-root desktop user (required for start)")
    parser.add_argument("--terminal", action="store_true")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("run with sudo on the Linux PCIe host")
    if args.action == "start" and not args.user:
        parser.error("start requires --user")
    if args.action == "stop":
        stop()
    else:
        start(args.user, args.terminal)
