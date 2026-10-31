#!/usr/bin/env python3
"""Reset FTDI-compatible channel B to UART mode and capture without tty.

Run with both kernel interfaces detached; close then reattach them afterwards.
FT_SetBitMode(handle, 0, 0) follows FTDI's documented mode reset operation:
https://ftdichip.com/Support/Knowledgebase/ft_setbitmode.htm
"""
import ctypes as c
import os
import sys

if len(sys.argv) > 2:
    sys.exit("usage: capture_uart_d2xx.py [baud]")
baud = int(sys.argv[1]) if len(sys.argv) == 2 else 115200
if not 9600 <= baud <= 1000000:
    sys.exit("baud must be between 9600 and 1000000")

library = os.environ.get("FTD2XX_LIBRARY",
    os.path.expanduser("~/Gowin/Programmer/V1.9.12.04/bin/libftd2xx.so"))
ft = c.CDLL(library)
count = c.c_uint32()
def checked(name, *args):
    status = getattr(ft, name)(*args)
    if status:
        raise RuntimeError(f"{name}: FT_STATUS={status}")

checked("FT_CreateDeviceInfoList", c.byref(count))
if count.value != 2:
    sys.exit(f"Expected exactly one dual-channel FT2232, found {count.value} channels")
handle = c.c_void_p()
checked("FT_Open", 1, c.byref(handle))
try:
    kind, identity = c.c_uint32(), c.c_uint32()
    serial, description = c.create_string_buffer(64), c.create_string_buffer(128)
    checked("FT_GetDeviceInfo", handle, c.byref(kind), c.byref(identity),
            serial, description, None)
    if identity.value != 0x04036010 or not (
            serial.value.endswith(b"B") or description.value.endswith(b"B")):
        raise RuntimeError("Channel B identity could not be established")
    print("UART channel:", serial.value.decode(), description.value.decode(), flush=True)
    checked("FT_SetBitMode", handle, c.c_ubyte(0), c.c_ubyte(0))
    checked("FT_SetBaudRate", handle, c.c_uint32(baud))
    checked("FT_SetDataCharacteristics", handle, c.c_ubyte(8), c.c_ubyte(0), c.c_ubyte(0))
    checked("FT_SetFlowControl", handle, c.c_ushort(0), c.c_ubyte(0), c.c_ubyte(0))
    checked("FT_SetTimeouts", handle, c.c_uint32(1000), c.c_uint32(1000))
    checked("FT_Purge", handle, c.c_uint32(3))
    for _ in range(3):
        buffer = c.create_string_buffer(512)
        received = c.c_uint32()
        checked("FT_Read", handle, buffer, c.c_uint32(len(buffer)), c.byref(received))
        print(f"{received.value} bytes:", repr(buffer.raw[:received.value]), flush=True)
finally:
    ft.FT_Close(handle)
