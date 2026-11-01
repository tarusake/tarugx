#!/usr/bin/env python3
"""CSRの字句検査。レジスタの意味やハードウェア適合性は検証しない。"""
import argparse
from pathlib import Path
import re


WRITE = re.compile(
    r"upar_(?:write|wirte)_driver\(\s*0x[0-9a-fA-F]{1,8}\s*,"
    r"\s*0x[0-9a-fA-F]{1,8}\s*\)"
)


def validate(text: str) -> int:
    count = 0
    for number, line in enumerate(text.splitlines(), 1):
        statement = line.split("//", 1)[0].strip()
        if not statement:
            continue
        if not WRITE.fullmatch(statement):
            raise ValueError(f"行{number}: 未対応または不正なCSR書込み: {statement}")
        count += 1
    if not count:
        raise ValueError("CSR書込みがありません")
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csr", type=Path)
    args = parser.parse_args()
    try:
        count = validate(args.csr.read_text())
    except (OSError, ValueError) as error:
        parser.exit(1, f"CSR検査失敗: {error}\n")
    print(f"CSR字句検査成功: {count}書込み（意味・適合性は未検査）")


if __name__ == "__main__":
    main()
