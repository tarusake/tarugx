#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Release/rebind the two FT2232 interfaces for Gowin Programmer / UART."""
import ctypes as c
import sys

if len(sys.argv) != 2 or sys.argv[1] not in ("attach", "detach"):
    sys.exit("usage: ftdi-driver.py attach|detach")
usb = c.CDLL("libusb-1.0.so.0")
usb.libusb_init.argtypes = [c.POINTER(c.c_void_p)]
usb.libusb_open_device_with_vid_pid.argtypes = [c.c_void_p, c.c_uint16, c.c_uint16]
usb.libusb_open_device_with_vid_pid.restype = c.c_void_p
usb.libusb_close.argtypes = [c.c_void_p]
usb.libusb_exit.argtypes = [c.c_void_p]
op = getattr(usb, "libusb_" + sys.argv[1] + "_kernel_driver")
op.argtypes = [c.c_void_p, c.c_int]
ctx = c.c_void_p()
if usb.libusb_init(c.byref(ctx)) != 0:
    sys.exit("libusb initialization failed")
handle = usb.libusb_open_device_with_vid_pid(ctx, 0x0403, 0x6010)
if not handle:
    usb.libusb_exit(ctx)
    sys.exit("FT2232 0403:6010 not accessible")
try:
    results = [op(handle, n) for n in (0, 1)]
    print(sys.argv[1], results)
    # NOT_FOUND means already detached; BUSY means already bound on attach.
    acceptable = (0, -5) if sys.argv[1] == "detach" else (0, -6)
    if any(value not in acceptable for value in results):
        sys.exit(1)
finally:
    usb.libusb_close(handle)
    usb.libusb_exit(ctx)
