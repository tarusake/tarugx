#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Global Resetの生成差分を確認する。実機用CSRやFlashは変更しない。"""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-mod", type=Path, required=True)
parser.add_argument("--gowin-root", type=Path, default=Path(os.environ.get("GOWIN_ROOT", os.path.expanduser("~/Gowin/V1.9.12.03"))))
parser.add_argument("--output", type=Path, default=Path("build/serdes-global-reset-probe/repro"))
args = parser.parse_args()
source = args.source_mod.read_text()
if "-type SERDES" not in source or "protocol_module_type pcie" not in source:
    parser.error("PCIe用SERDESの.modが必要です")
# 既存の証跡や生成物を上書きしない。
args.output.mkdir(parents=True, exist_ok=False)
ide = args.gowin_root / "IDE"
env = os.environ.copy()
env.update(QT_QPA_PLATFORM="offscreen", LD_LIBRARY_PATH=str(ide / "lib"),
           LD_PRELOAD="/lib/x86_64-linux-gnu/libstdc++.so.6:/lib/x86_64-linux-gnu/libfreetype.so.6")
results = {}
for mode in ("disable", "enable"):
    out = (args.output / mode).resolve()
    out.mkdir()
    config = re.sub(r"^-path .*", "-path " + str(out) + "/", source, flags=re.M)
    config = re.sub(r"^-global_reset .*\n?", "", config, flags=re.M)
    config = config.replace("-protocol_module_name0", "-global_reset " + mode + "\n-protocol_module_name0", 1)
    mod = out / "serdes.mod"
    mod.write_text(config)
    run = subprocess.run([str(ide / "bin/GowinModGen"), "-do", str(mod)],
                         env=env, capture_output=True, text=True, timeout=60)
    (out / "generate.log").write_text(run.stdout + run.stderr)
    run.check_returncode()
    rtl = out / "serdes.v"
    results[mode] = re.sub(r"^//Created Time:.*\n", "", rtl.read_text(), flags=re.M)
assert "input por_n_i;" in results["enable"], "Global Resetポートが生成されていません"
assert "input por_n_i;" not in results["disable"]
for port in ["FABRIC_POR_N_I", "FABRIC_CMU0_RESETN_I", "FABRIC_CMU1_RESETN_I"] + [f"FABRIC_LN{i}_CPLL_RESETN_I" for i in range(4)]:
    assert f".{port}(por_n_i)" in results["enable"], port
    assert f".{port}(gw_gnd)" in results["disable"], port
(args.output / "reset.diff").write_text("".join(difflib.unified_diff(
    results["disable"].splitlines(True), results["enable"].splitlines(True),
    fromfile="disable/serdes.v", tofile="enable/serdes.v")))
print("生成・接続差分の検証成功。CSR未生成のため実機へは未適用。")
