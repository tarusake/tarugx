#!/usr/bin/env python3
"""Convert an EFI Option ROM to little-endian DWORDs for $readmemh.

This only prepares storage contents; it does not enable PCIe ROM decoding.
"""
import argparse
from pathlib import Path
import struct


def memory_text(data: bytes, aperture_size: int) -> str:
    if aperture_size < 2048 or aperture_size & (aperture_size - 1):
        raise ValueError('aperture must be a power of two, at least 2048 bytes')
    if len(data) != aperture_size:
        raise ValueError('ROM size must exactly match the configured aperture')
    if data[:2] != b'\x55\xaa' or sum(data) & 255:
        raise ValueError('invalid ROM signature or checksum')
    if struct.unpack_from('<I', data, 4)[0] != 0xef1:
        raise ValueError('not an EFI Option ROM')
    if struct.unpack_from('<HHH', data, 8) != (11, 0x8664, 0):
        raise ValueError('expected an uncompressed X64 boot-service driver')
    image, pcir = struct.unpack_from('<HH', data, 0x16)
    if pcir < 0x1c or pcir + 24 > len(data) or data[pcir:pcir+4] != b'PCIR':
        raise ValueError('invalid PCIR pointer')
    if struct.unpack_from('<HH', data, pcir+4) != (0x22c2, 0x1100):
        raise ValueError('unexpected PCI ID')
    if struct.unpack_from('<H', data, pcir+16)[0] * 512 != len(data):
        raise ValueError('PCIR image length mismatch')
    if struct.unpack_from('<H', data, 2)[0] * 512 != len(data):
        raise ValueError('EFI image length mismatch')
    if data[pcir+20:pcir+22] != bytes([3, 0x80]):
        raise ValueError('expected one final EFI image')
    if image < pcir+24 or image+2 > len(data) or data[image:image+2] != b'MZ':
        raise ValueError('invalid EFI payload offset')
    return ''.join(f'{word:08x}\n' for (word,) in struct.iter_unpack('<I', data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--aperture-size', type=lambda v: int(v, 0), default=65536)
    args = parser.parse_args()
    if args.rom.resolve() == args.output.resolve():
        parser.error('input and output must differ')
    try:
        result = memory_text(args.rom.read_bytes(), args.aperture_size)
    except ValueError as error:
        parser.error(str(error))
    args.output.write_text(result, encoding='ascii')
    print(f'{args.output.name}: {len(result.splitlines())} little-endian DWORDs')


if __name__ == '__main__':
    main()
