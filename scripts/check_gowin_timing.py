#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Check Gowin's summary AND inter-clock setup/hold/reset path tables."""
import html
from pathlib import Path
import re
import sys

def cells(row):
    return [html.unescape(re.sub(r"<[^>]+>", "", x)).strip()
            for x in re.findall(r"<t[dh]\b[^>]*>(.*?)</t[dh]>", row, re.S)]

def table_after(text, anchor):
    match = re.search(r'name="' + re.escape(anchor) + r'".*?(<table\b.*?</table>)',
                      text, re.S)
    if not match:
        raise ValueError("missing timing table: " + anchor)
    return [cells(x) for x in re.findall(r"<tr\b[^>]*>(.*?)</tr>",
                                        match.group(1), re.S)]

def main():
    if len(sys.argv) != 2:
        raise ValueError("usage: check_gowin_timing.py tarugx_tr_content.html")
    text = Path(sys.argv[1]).read_text()
    failed = False
    for row in table_after(text, "Total_Negative_Slack_Report")[1:]:
        if len(row) != 4:
            raise ValueError("unexpected TNS table format")
        if float(row[2]) < 0:
            print(f"TNS violation: {row[0]} {row[1]} {row[2]} ns, {row[3]} endpoints")
            failed = True
    for kind in ("Setup", "Hold", "Recovery", "Removal"):
        rows = table_after(text, kind + "_Slack_Table")[1:]
        if not rows:
            raise ValueError("empty path table: " + kind)
        minimum = min(float(row[1]) for row in rows)
        print(f"{kind}: minimum reported slack {minimum:.3f} ns")
        for row in rows:
            if float(row[1]) < 0:
                print(f"  {row[1]} ns: {row[2]} -> {row[3]}")
                failed = True
    return 1 if failed else 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, IndexError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
