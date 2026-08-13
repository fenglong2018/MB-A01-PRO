#!/usr/bin/env python3
"""
尝试修复「UTF-8 被当成 GBK 打开后又另存为 xlsx」造成的中文乱码。

用法:
  py -3 -m pip install openpyxl
  py -3 fix_xlsx_mojibake.py 乱码文件.xlsx
  py -3 fix_xlsx_mojibake.py 乱码文件.xlsx -o 修复后.xlsx

说明:
  - 会生成新文件，不覆盖原文件
  - 若预览仍不对，可加 --mode gbk_as_utf8 再试另一种常见误开方式
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def fix_text(s: str, mode: str) -> str:
    if not isinstance(s, str) or not s:
        return s
    try:
        if mode == "utf8_as_gbk":
            # 常见：UTF-8 被按 GBK/GB18030 解开后存进 xlsx
            try:
                return s.encode("gb18030").decode("utf-8")
            except (UnicodeEncodeError, UnicodeDecodeError):
                return s.encode("gbk").decode("utf-8")
        if mode == "gbk_as_utf8":
            # 较少见：GBK 被按 UTF-8 解开
            return s.encode("latin1").decode("gbk")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return s
    return s


def main() -> int:
    parser = argparse.ArgumentParser(description="修复 xlsx 中文乱码（尽力）")
    parser.add_argument("input", type=Path, help="乱码 xlsx 路径")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="输出路径（默认: 原名_fixed.xlsx）",
    )
    parser.add_argument(
        "--mode",
        choices=("utf8_as_gbk", "gbk_as_utf8"),
        default="utf8_as_gbk",
        help="乱码成因假设，默认 utf8_as_gbk",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        help="只预览前几处修复效果，不写文件",
    )
    args = parser.parse_args()

    try:
        from openpyxl import load_workbook
    except ImportError:
        print("请先安装: py -3 -m pip install openpyxl", file=sys.stderr)
        return 1

    src = args.input
    if not src.is_file():
        print(f"找不到文件: {src}", file=sys.stderr)
        return 1

    wb = load_workbook(src)
    changed = 0
    samples: list[tuple[str, str]] = []

    for ws in wb.worksheets:
        for row in ws.iter_rows():
            for cell in row:
                val = cell.value
                if not isinstance(val, str):
                    continue
                fixed = fix_text(val, args.mode)
                if fixed != val:
                    changed += 1
                    if len(samples) < 8:
                        samples.append((val, fixed))
                    if not args.preview:
                        cell.value = fixed

    print(f"模式: {args.mode}")
    print(f"可改写单元格约: {changed}")
    if samples:
        print("预览（乱码 → 尝试修复）:")
        for a, b in samples:
            print(f"  {a!r}")
            print(f"  => {b!r}")
    else:
        print("没有检测到可按该模式修复的文本。可换 --mode gbk_as_utf8 再试。")
        return 2

    if args.preview:
        print("（仅预览，未写文件）")
        return 0

    out = args.output or src.with_name(src.stem + "_fixed.xlsx")
    wb.save(out)
    print(f"已写出: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
