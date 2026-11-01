import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "decoder", Path(__file__).resolve().parents[1] / "tools/decode_scanout_uart.py")
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class DecodeTest(unittest.TestCase):
    def test_fr16_pipeline_fault_layout(self):
        data=decoder.decode("D1110X46523136" + "0" * 64)
        self.assertTrue(data["automatic_rom"])
        self.assertFalse(data["fault_captured"])
        self.assertNotIn("performance",data)

    def test_fr14_performance(self):
        data=decoder.decode("D1110X46523134" + "0" * 64)
        self.assertTrue(data["automatic_rom"])
        self.assertFalse(data["performance"]["frozen"])

    def test_fr13_performance(self):
        perf = (17 << 168) | (33 << 152) | (29 << 136) | (5 << 120)
        perf |= (1000 << 88) | (1200 << 56) | (333 << 24) | (3 << 22) | (2 << 10) | 512
        payload = (0x46523133 << 256) | (perf << 72) | (0x28001006400640ac << 8) | 0xfd
        data = decoder.decode(f"D1111X{payload:072X}")
        self.assertEqual(data["performance"], dict(
            max_scan_wait_cycles=17, max_scan_response_cycles=33,
            max_memory_read_cycles=29, max_memory_write_cycles=5,
            scan_requests=1000, memory_requests=1200, scan_wait_cycles=333,
            frozen=True, level_armed=True, min_level_upper_estimate=2,
            max_level_upper_estimate=512))
        self.assertTrue(data["automatic_rom"])
        self.assertEqual((data["x"], data["y"]), (160, 1))
        self.assertNotIn("pcie", data)

    def test_fr12_scanout_fault(self):
        payload = (0x46523132 << 256) | (0x28001006400640ac << 8) | 0xfd
        data = decoder.decode(f"D1111X{payload:072X}")
        self.assertTrue(data["automatic_rom"])
        self.assertTrue(data["phy_recovery"])
        self.assertTrue(data["fault_captured"])
        self.assertEqual((data["x"], data["y"], data["expected"], data["actual"]),
                         (160, 1, 100, 100))
        self.assertEqual(data["fifo_valid"], 0)
        self.assertNotIn("rom_probe", data)

    def test_injected_fault(self):
        data = decoder.decode("D1111X46523033" + "0" * 46 + "28001006400640ACFD\r\n")
        self.assertEqual((data["x"], data["y"], data["expected"], data["actual"]),
                         (160, 1, 100, 100))
        self.assertTrue(data["fault_captured"])
        self.assertEqual(data["fifo_valid"], 0)

    def test_pcie_snapshot(self):
        pci = int("8000000307020304011fffad", 16)
        payload = (0x46523034 << 256) | (pci << 72) | 0xfd
        data = decoder.decode(f"D1100X{payload:072X}")
        p = data["pcie"]
        self.assertEqual(p["visited"], "80000003")
        self.assertEqual([p[k] for k in ("transitions", "perst_falls", "cmu_falls", "link_falls", "previous_ltssm", "ltssm")], [7, 2, 3, 4, 1, 31])
        self.assertEqual(p["seen_flags"], "ad")
        self.assertEqual(p["tlp_clock_alive"], 1)
        self.assertEqual(p["perst_n"], 1)
        self.assertEqual(p["link"], 1)
        self.assertEqual(data["link"], 0)
        self.assertFalse(data["fault_captured"])

    def test_recovery_status(self):
        payload = (0x46523035 << 256) | (5 << 168)
        data = decoder.decode(f"D1100X{payload:072X}")
        self.assertEqual(data["recovery"], {"fired": 1, "inhibited": 0, "reset_active": 1})
        self.assertEqual(data["pcie"]["visited"], "00000000")

    def test_reset_scope(self):
        payload = (0x46523036 << 256) | (0x031e031d1f011f << 176) | (1 << 168)
        data = decoder.decode(f"D1100X{payload:072X}")
        self.assertEqual(data["reset_scope"], dict(flags=3, before=30, during_high=3,
                         during_low=29, after_high=31, after_low=1, current=31))
        self.assertEqual(data["recovery"]["fired"], 1)
        self.assertEqual(data["pcie"]["visited"], "00000000")

    def test_waiting_snapshot(self):
        data = decoder.decode("D1111X46523033" + "0" * 62 + "FD")
        self.assertFalse(data["fault_captured"])
        self.assertNotIn("x", data)

    def test_phy_recovery_tag(self):
        payload = (0x46523037 << 256) | (0x031e031d1f011f << 176) | (5 << 168)
        data = decoder.decode(f"D1100X{payload:072X}")
        self.assertTrue(data["phy_recovery"])
        self.assertEqual(data["recovery"], {"fired": 1, "inhibited": 0, "reset_active": 1})
        self.assertEqual(data["reset_scope"]["flags"], 3)
        self.assertEqual(data["reset_scope"]["current"], 31)

    def test_reject_partial_and_old_version(self):
        self.assertIsNone(decoder.decode("D1111X123"))
        self.assertIsNone(decoder.decode("D1111X46523032" + "0" * 64))


class RomProbeDecodeTest(unittest.TestCase):
    def test_fr11_tag(self):
        result=decoder.decode("D1110X46523131" + "0" * 59 + "00040")
        self.assertEqual(result["rom_probe"]["raw_bar"],"00040")

    def test_fr10_raw_bar(self):
        payload=(0x46523130<<256)|0x80040
        result=decoder.decode(f"D1110X{payload:072X}")["rom_probe"]
        self.assertEqual(result["raw_bar"],"80040")
        self.assertEqual(len(result["dwords"]),4)

    def test_fr09_tag(self):
        self.assertIsNotNone(decoder.decode("D1110X46523039" + "0" * 64))

    def test_fr08_layout(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('probe_decoder',Path(__file__).parents[1]/'tools/decode_scanout_uart.py')
        m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        payload=(0x46523038<<256)|(7<<224)|(2<<192)|(15<<184)|(0xe0<<168)|(16<<160)
        for shift,value in zip((128,96,64,32,0),(1,2,3,4,5)):payload|=value<<shift
        result=m.decode(f'D1110X{payload:072X}')['rom_probe']
        self.assertEqual(result['accepted_sops'],7)
        self.assertEqual(result['unmatched_sops'],2)
        self.assertTrue(result['captured'] and result['unmatched'])
        self.assertEqual(result['error'],16)
        self.assertEqual(result['dwords'],[f'{x:08x}' for x in (1,2,3,4,5)])

if __name__ == "__main__":
    unittest.main()
