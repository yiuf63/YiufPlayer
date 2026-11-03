#!/usr/bin/env python3
"""校验项目中用于 UI 渲染的字体是否满足 nanoVG / stb_truetype 的要求。

为什么需要这个脚本：
    换字体时踩过一个非常隐蔽的坑 —— 用了 Noto Sans SC **可变字体**，
    结果「只要 OSD 上出现中文，整个 nanoVG 帧就渲染失败，连视频都变成全黑」。
    现象看起来像视频解码坏了，实际是字体格式不被支持。

三条硬性要求（缺一不可）：
    1. 不含 fvar 表        —— stb_truetype 不支持可变字体
    2. 含 glyf 表          —— 只支持 TrueType 轮廓，不支持 CFF/OTTO
    3. CJK 覆盖 >= 20000   —— 否则常用汉字缺失，显示为方块

用法：
    python3 scripts/check_font.py                 # 检查默认字体
    python3 scripts/check_font.py path/to/font.ttf
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_FONTS = [
    ROOT / "resources" / "font" / "font.ttf",
    ROOT / "resources" / "font" / "icon.ttf",
    ROOT / "resources" / "romfs" / "font" / "font.ttf",
]

REQUIRED_CJK = 20000


def read_tables(data: bytes) -> set[str]:
    """从 sfnt 头读出所有表标签。"""
    if len(data) < 12:
        return set()
    num_tables = struct.unpack(">H", data[4:6])[0]
    tags = set()
    for i in range(num_tables):
        off = 12 + i * 16
        if off + 4 > len(data):
            break
        tags.add(data[off:off + 4].decode("latin-1").strip())
    return tags


def count_cjk(data: bytes) -> int:
    """粗算 cmap 里的 CJK 码位数（只解析 format 4 / 12）。"""
    num_tables = struct.unpack(">H", data[4:6])[0]
    cmap_off = None
    for i in range(num_tables):
        off = 12 + i * 16
        if data[off:off + 4] == b"cmap":
            cmap_off = struct.unpack(">I", data[off + 8:off + 12])[0]
            break
    if cmap_off is None:
        return 0

    n_sub = struct.unpack(">H", data[cmap_off + 2:cmap_off + 4])[0]
    codepoints: set[int] = set()
    for i in range(n_sub):
        rec = cmap_off + 4 + i * 8
        _, _, sub_off = struct.unpack(">HHI", data[rec:rec + 8])
        base = cmap_off + sub_off
        fmt = struct.unpack(">H", data[base:base + 2])[0]
        if fmt == 4:
            seg_x2 = struct.unpack(">H", data[base + 6:base + 8])[0]
            seg = seg_x2 // 2
            ends = struct.unpack(f">{seg}H", data[base + 14:base + 14 + seg_x2])
            starts_off = base + 16 + seg_x2
            starts = struct.unpack(f">{seg}H", data[starts_off:starts_off + seg_x2])
            for a, b in zip(starts, ends):
                if b == 0xFFFF:
                    continue
                for c in range(a, min(b, 0xFFFF) + 1):
                    codepoints.add(c)
        elif fmt == 12:
            n_groups = struct.unpack(">I", data[base + 12:base + 16])[0]
            for g in range(min(n_groups, 8000)):
                go = base + 16 + g * 12
                a, b, _ = struct.unpack(">III", data[go:go + 12])
                for c in range(a, min(b, a + 2000) + 1):
                    codepoints.add(c)
    return sum(1 for c in codepoints if 0x4E00 <= c <= 0x9FFF)


def check(path: Path) -> list[str]:
    errors = []
    if not path.exists():
        return [f"文件不存在: {path}"]
    data = path.read_bytes()
    tags = read_tables(data)

    if "fvar" in tags:
        errors.append("含 fvar 表（可变字体）—— stb_truetype 不支持，会导致整帧渲染失败")
    if "glyf" not in tags:
        errors.append("缺少 glyf 表 —— 不是 TrueType 轮廓（CFF/OTTO 不支持）")
    if "CFF " in tags:
        errors.append("含 CFF 表 —— stb_truetype 不支持 CFF 轮廓")

    # icon.ttf 是图标字体，不要求中文覆盖
    if path.name != "icon.ttf":
        cjk = count_cjk(data)
        if cjk < REQUIRED_CJK:
            errors.append(f"CJK 覆盖不足: {cjk} < {REQUIRED_CJK}（会出现方块）")
    return errors


def main() -> int:
    paths = [Path(p) for p in sys.argv[1:]] or DEFAULT_FONTS
    failed = 0
    for path in paths:
        if not path.exists():
            print(f"  跳过（不存在）: {path}")
            continue
        try:
            errors = check(path)
        except Exception as exc:                      # noqa: BLE001
            print(f"  ✗ {path.name}: 解析失败 {exc}")
            failed += 1
            continue
        rel = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        if errors:
            print(f"  ✗ {rel}")
            for e in errors:
                print(f"      · {e}")
            failed += 1
        else:
            size_kb = path.stat().st_size // 1024
            print(f"  ✓ {rel}  ({size_kb} KB)")

    if failed:
        print(f"\n字体检查未通过：{failed} 个文件")
        return 1
    print("\n字体检查通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
