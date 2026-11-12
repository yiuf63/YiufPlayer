#!/usr/bin/env python3
"""分析 Muvik 抓取的帧缓冲（PPM），验证界面是否真的渲染出来。

配合程序内的 FrameGrabber 使用：

    MUVIK_FRAMEGRAB=/tmp/grab.ppm ./build/muvik
    python3 tests/tools/analyze_framegrab.py /tmp/grab.ppm

为什么需要这个工具：
    macOS 上 `screencapture` 需要屏幕录制权限，在无权限、显示器休眠
    或 CI 环境下会返回纯黑图。从程序内部读 GL 帧缓冲可以完全绕过
    这个限制，是验证 UI 渲染最可靠的方式。
"""

import sys
import numpy as np
from PIL import Image


def load_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"不是 P6 PPM: {magic}")
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        f.readline()  # maxval
        data = np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)
    return data


def ascii_layout(gray, cols=100, rows=30):
    h, w = gray.shape
    lines = []
    lines.append("=" * (cols + 2))
    for ty in range(rows):
        row = "|"
        for tx in range(cols):
            y0, y1 = ty * h // rows, (ty + 1) * h // rows
            x0, x1 = tx * w // cols, (tx + 1) * w // cols
            blk = gray[y0:y1, x0:x1]
            mx, avg = blk.max(), blk.mean()
            if mx > 180:
                row += "#"      # 文字 / 高亮
            elif mx > 100:
                row += "+"      # 次级元素
            elif avg > 52:
                row += "."      # 略亮的背景块
            else:
                row += " "      # 深色背景
        lines.append(row + "|")
    lines.append("=" * (cols + 2))
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    path = sys.argv[1]
    data = load_ppm(path)
    h, w, _ = data.shape
    gray = data.mean(axis=2)

    print(f"画面尺寸: {w}x{h}")

    # --- 1. 基本统计 ---
    print(f"\n亮度: 均值 {gray.mean():.1f}  最暗 {gray.min():.0f}  最亮 {gray.max():.0f}")

    mx = data.max(axis=2).astype(int)
    mn = data.min(axis=2).astype(int)
    low_sat = ((mx - mn) < 18).mean() * 100
    print(f"中性灰像素: {low_sat:.2f}%   （Borealis 深色主题应 >90%）")

    # --- 2. 判定是否真的渲染了 UI ---
    print("\n--- 渲染判定 ---")
    ok = True
    if low_sat < 90:
        print(f"  ✗ 中性灰占比过低 ({low_sat:.1f}%)，可能未渲染或抓到的是桌面内容")
        ok = False
    else:
        print(f"  ✓ 中性灰占比 {low_sat:.1f}%，符合深色主题")

    bright = gray > 120
    text_ratio = bright.mean() * 100
    if text_ratio < 0.5:
        print(f"  ✗ 亮像素仅 {text_ratio:.2f}%，几乎没有文字")
        ok = False
    else:
        print(f"  ✓ 亮像素 {text_ratio:.2f}%，存在文字或图标")

    # --- 3. 结构元素 ---
    print("\n--- 结构元素 ---")

    # 贯穿屏幕的水平分隔线
    row_strong = (gray > 150).mean(axis=1)
    hlines = [y for y in range(h) if row_strong[y] > 0.5]
    if hlines:
        # 合并相邻行
        groups, cur = [], [hlines[0]]
        for y in hlines[1:]:
            if y - cur[-1] <= 2:
                cur.append(y)
            else:
                groups.append(cur)
                cur = [y]
        groups.append(cur)
        print(f"  水平分隔线: {len(groups)} 条")
        for gp in groups:
            print(f"    y={gp[0]}..{gp[-1]}")
    else:
        print("  水平分隔线: 未检测到")

    # 垂直边界（侧边栏）
    dx = np.abs(np.diff(gray, axis=1))
    col_edges = (dx > 8).sum(axis=0)
    if col_edges.max() > h * 0.2:
        top = np.argsort(col_edges)[-5:][::-1]
        print(f"  垂直边界候选列: {sorted(top.tolist())}")

    # --- 4. 元素行带 ---
    row_b = bright.mean(axis=1)
    runs, s = [], None
    for y in range(h):
        if row_b[y] > 0.002:
            if s is None:
                s = y
        else:
            if s is not None:
                runs.append((s, y - 1))
                s = None
    if s is not None:
        runs.append((s, h - 1))

    print(f"\n  元素行带: {len(runs)} 个")
    for a, b in runs:
        band = bright[a:b + 1, :]
        bx = np.where(band.any(axis=0))[0]
        xr = f"x={bx.min()}-{bx.max()}" if len(bx) > 0 else "-"
        print(f"    y={a:3d}-{b:3d} 高{b - a + 1:3d}  {xr}")

    # --- 5. ASCII 布局 ---
    print("\n--- 布局概览 ---")
    print(ascii_layout(gray))
    print("\n图例: # = 文字/高亮  + = 次级元素  . = 略亮区域  空格 = 深色背景")

    print(f"\nRESULT: {'PASS 界面已正常渲染' if ok else 'FAIL 界面未正常渲染'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
