# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
import importlib.util
from pathlib import Path
import struct
import unittest
from test_efi_rom import rom, image

spec = importlib.util.spec_from_file_location('rom_mem', Path(__file__).parents[1] / 'tools/make_rom_mem.py')
mem = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mem)


class RomMemoryTest(unittest.TestCase):
    def test_every_byte_roundtrips(self):
        data = rom.build_rom(image())
        words = mem.memory_text(data, len(data)).splitlines()
        self.assertEqual(len(words), len(data)//4)
        self.assertEqual(b''.join(struct.pack('<I', int(w, 16)) for w in words), data)
        self.assertEqual(int(words[0],16)&0xffff, 0xaa55)
        self.assertEqual(int(words[128],16)&0xffff, 0x5a4d)

    def test_bad_image_and_aperture(self):
        data = rom.build_rom(image())
        for size in [0, 1024, 2047, 4096]:
            with self.assertRaises(ValueError): mem.memory_text(data, size)
        for offset in [0, 2, 4, 8, 10, 12, 0x16, 0x18, 0x20, 0x2c, 0x30, 512]:
            bad = bytearray(data)
            bad[offset] ^= 1
            # Also test semantic rejection with an otherwise valid checksum.
            bad[-1] = (bad[-1] - sum(bad)) & 255
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                mem.memory_text(bytes(bad), len(bad))
        bad = bytearray(data); bad[600] ^= 1
        with self.assertRaises(ValueError): mem.memory_text(bytes(bad), len(bad))


if __name__ == '__main__': unittest.main()
