import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('rom', Path(__file__).parents[1] / 'tools/make_efi_rom.py')
rom = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rom)


def image():
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, 0x8664)
    struct.pack_into('<H', data, 0x94, 240)
    struct.pack_into('<H', data, 0x98, 0x20b)
    struct.pack_into('<H', data, 0x98+68, 11)
    return data


class RomTest(unittest.TestCase):
    def test_layout_and_checksum(self):
        payload = image()
        data = rom.build_rom(payload)
        self.assertEqual(len(data), 2048)
        self.assertEqual(sum(data) & 255, 0)
        self.assertEqual(struct.unpack_from('<HHIHHH', data), (0xaa55, 4, 0xef1, 11, 0x8664, 0))
        self.assertEqual(struct.unpack_from('<HH', data, 0x16), (512, 0x1c))
        self.assertEqual(data[512:1536], payload)
        self.assertEqual(data[0x1c:0x20], b'PCIR')
        self.assertEqual(struct.unpack_from('<HH', data, 0x20), (0x22c2, 0x1100))
        self.assertEqual(data[0x29:0x2c], bytes([0, 0x80, 3]))
        self.assertEqual(struct.unpack_from('<H', data, 0x2c)[0], len(data)//512)
        self.assertEqual(data[0x30:0x32], bytes([3, 0x80]))

    def test_bad_images(self):
        cases = [b'', bytes(64), image()[:130]]
        for offset, value in [(0x3c, 0xffffffff), (0x84, 0x14c), (0x94, 0xffff), (0x98+68, 10)]:
            p = image()
            struct.pack_into('<I' if offset == 0x3c else '<H', p, offset, value)
            cases.append(p)
        for p in cases:
            with self.assertRaises(ValueError): rom.build_rom(p)

    def test_exact_boundary_keeps_payload(self):
        p = image() + bytes(512)
        data = rom.build_rom(p)
        self.assertEqual(len(data), 4096)
        self.assertEqual(data[512:512+len(p)], p)


if __name__ == '__main__': unittest.main()
