#!/usr/bin/env python3
"""Wrap an X64 EFI boot-service driver in one uncompressed PCI option ROM.

This creates a file; it does not configure the FPGA's Expansion ROM BAR.
UEFI 2.11 section 14.4.2, PCI-compatible EFI option ROMs.
"""
import argparse
from pathlib import Path
import struct


def build_rom(efi: bytes) -> bytes:
    if len(efi) < 64 or efi[:2] != b'MZ':
        raise ValueError('not a PE image')
    pe = struct.unpack_from('<I', efi, 0x3c)[0]
    if pe < 64 or pe + 24 + 70 > len(efi) or efi[pe:pe+4] != b'PE\0\0':
        raise ValueError('invalid PE header')
    machine = struct.unpack_from('<H', efi, pe+4)[0]
    optional_size = struct.unpack_from('<H', efi, pe+20)[0]
    optional = pe+24
    if optional_size < 70 or optional+optional_size > len(efi):
        raise ValueError('truncated PE optional header')
    if machine != 0x8664 or struct.unpack_from('<H', efi, optional)[0] != 0x20b:
        raise ValueError('only X64 PE32+ is supported')
    subsystem = struct.unpack_from('<H', efi, optional+68)[0]
    if subsystem != 11:
        raise ValueError('expected EFI boot-service driver (subsystem 11)')
    image_offset, pcir_offset = 512, 0x1c
    # Leave padding for a checksum byte; round to a power-of-two ROM aperture.
    size = 1 << (image_offset + len(efi)).bit_length()
    blocks = size // 512
    if blocks > 0xffff:
        raise ValueError('ROM exceeds 16-bit image length')
    rom = bytearray(b'\xff' * size)
    struct.pack_into('<HHIHHH8sHH', rom, 0, 0xaa55, blocks, 0x0ef1,
                     subsystem, machine, 0, bytes(8), image_offset, pcir_offset)
    struct.pack_into('<4sHHHHB3sHHBBH', rom, pcir_offset,
                     b'PCIR', 0x22c2, 0x1100, 0, 24, 0,
                     bytes([0x00, 0x80, 0x03]), blocks, 0, 3, 0x80, 0)
    rom[image_offset:image_offset+len(efi)] = efi
    rom[-1] = 0
    rom[-1] = (-sum(rom)) & 255
    return bytes(rom)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('efi', type=Path)
    parser.add_argument('rom', type=Path)
    args = parser.parse_args()
    if args.efi.resolve() == args.rom.resolve():
        parser.error('input and output must differ')
    try:
        rom = build_rom(args.efi.read_bytes())
    except ValueError as e:
        parser.error(str(e))
    args.rom.write_bytes(rom)
    print(f'{args.rom.name}: {len(rom)} bytes, PCI 22c2:1100, X64 boot-service driver')


if __name__ == '__main__':
    main()
