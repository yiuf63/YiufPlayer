#!/usr/bin/env python3
"""核对用户文档与实际实现是否一致。

用户指南写错会直接误导使用者，比代码 bug 更伤。
本脚本把「文档承诺的东西」逐条对着源码/运行结果验一遍。

用法：
    python3 tests/tools/check_docs.py
    python3 tests/tools/check_docs.py -v    # 打印每条匹配到的源码位置
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "docs" / "使用指南.md"
SRC = ROOT / "src"

RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))


def read_all_sources():
    """把 src/ 下的 .cpp/.mm/.hpp 拼成一个大字符串，方便做存在性检查。"""
    buf = []
    for ext in ("*.cpp", "*.mm", "*.hpp"):
        for p in SRC.rglob(ext):
            buf.append(p.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(buf)


def find_occurrences(text, needle):
    """返回 needle 在文本中的位置（用于 -v 输出）。"""
    return [m.start() for m in re.finditer(re.escape(needle), text)]


def check_ui_strings(src_text, verbose):
    """文档里提到的界面文案，代码中必须真实存在。"""
    strings = [
        "添加媒体文件夹",
        "添加当前文件夹",
        "重新扫描媒体库",
        "插件管理",
        "从 ZIP 包安装插件",
        "重新扫描插件",
    ]
    for s in strings:
        hits = find_occurrences(src_text, s)
        check(f"界面文案存在：{s}", bool(hits),
              f"{len(hits)} 处" if hits else "源码中未找到")
        if verbose and hits:
            for h in hits[:1]:
                snippet = src_text[max(0, h - 60):h + 60].replace("\n", " ")
                print(f"    {s}: …{snippet}…")


def check_keybindings(src_text, verbose):
    """文档的「手柄按键速查」必须和 registerAction 一致。"""
    # 播放器里应注册的按键（与文档表格逐项对应）
    expected = {
        "BUTTON_A": "播放/暂停",
        "BUTTON_LEFT": "快退",
        "BUTTON_RIGHT": "快进",
        "BUTTON_LT": "大幅快退",
        "BUTTON_RT": "大幅快进",
        "BUTTON_LB": "上一集",
        "BUTTON_RB": "下一集",
        "BUTTON_UP": "播放列表",
        "BUTTON_X": "音轨选择",
        "BUTTON_Y": "字幕设置",
        "BUTTON_RSB": "倍速切换",
        "BUTTON_LSB": "比例切换",
        "BUTTON_START": "触控锁定",
        # 说明：全屏切换**没有**手柄绑定。
        # 它原本在 BUTTON_BACK（手柄 Select）上，但 Android 的返回键
        # 在 SDL 里也映射到 BUTTON_BACK —— 用户按「返回」期望退出播放页，
        # 实际却在切全屏，返回事件永远到不了 Activity 栈。
        # 现在全屏只通过 OSD 的「画面」按钮切换，BACK 恢复为「返回」。
        # 因此这里不再断言 BACK → 全屏切换。
    }
    for btn, action in expected.items():
        pat = re.compile(
            r'registerAction\("' + re.escape(action) + r'"\s*,\s*'
            r'brls::ControllerButton::' + btn)
        m = pat.search(src_text)
        check(f"按键 {btn.replace('BUTTON_', ''):6s} → {action}", bool(m),
              "已注册" if m else "未匹配到 registerAction")
        if verbose and m:
            line_start = src_text.rfind("\n", 0, m.start()) + 1
            line_end = src_text.find("\n", m.start())
            print(f"    {src_text[line_start:line_end].strip()[:90]}")


def check_behaviors(src_text, doc_text, verbose):
    """文档描述的具体数值必须和代码一致。"""
    # 快进/快退秒数：必须【文档与代码一致】，只看一边说明不了问题。
    # 实测教训：只检查源码时，把文档里的 "10 秒" 改成 "15 秒" 也照样通过。
    m = re.search(r"快退\s*/\s*快进\s*(\d+)\s*秒", doc_text)
    doc_step = m.group(1) if m else None
    check("文档写明方向键步长", bool(doc_step), f"{doc_step} 秒" if doc_step else "未找到")
    if doc_step:
        check("方向键步长 文档=代码",
              f"seek(-{doc_step})" in src_text and f"seek({doc_step})" in src_text,
              f"文档 {doc_step} / 代码 seeks")

    m = re.search(r"\*\*LT / RT\*\*\s*\|\s*快退 / 快进\s*(\d+)\s*秒", doc_text)
    doc_big = m.group(1) if m else None
    check("文档写明 LT/RT 步长", bool(doc_big), f"{doc_big} 秒" if doc_big else "未找到")
    if doc_big:
        check("LT/RT 步长 文档=代码",
              f"seek(-{doc_big})" in src_text and f"seek({doc_big})" in src_text,
              f"文档 {doc_big} / 代码 seeks")

    # 倍速循环序列
    m = re.search(r"speeds\[\]\s*=\s*\{([^}]+)\}", src_text)
    if m:
        got = [x.strip() for x in m.group(1).split(",")]
        # 文档里写在代码块：`1.0x → 1.25x → 1.5x → 2.0x → 0.75x`
        dm = re.search(r"`([0-9.x\s→]+)`", doc_text)
        doc_seq = None
        if dm:
            doc_seq = [x.replace("x", "") for x in
                       re.findall(r"[0-9.]+x", dm.group(1))]
        check("倍速序列 文档=代码", doc_seq == got,
              f"文档 {doc_seq} / 代码 {got}")
    else:
        check("倍速序列 文档=代码", False, "未找到 speeds[]")

    # 续播门槛
    check("续播门槛 15 秒", "rec.position > 15" in src_text, "")

    # 已看完判定
    has_near = "near_end" in src_text
    has_half = "duration / 2" in src_text
    check("已看完判定含「距结尾 30 秒」", has_near, "")
    check("已看完判定含「播过一半」下限", has_half, "")

    # 手势灵敏度：按屏幕高度比例，而不是固定像素。
    #
    # 原来的判据断言「满量程 300 像素」，但固定像素值在不同尺寸的设备上
    # 手感差异极大（1280x906 上滑三分之一屏就到头，小屏要滑很多次）。
    # 现已改为按 view 高度比例，因此判据同步改为检查这个比例关系。
    m = re.search(r"sensitivity\s*=\s*view_h\s*\*\s*([0-9.]+)f", src_text)
    check("上下滑动灵敏度按屏高比例", bool(m),
          f"系数 {m.group(1)}" if m else "未找到 sensitivity = view_h * N 的写法")
    check("水平滑动跨度为半时长", "total_duration_ * 0.5" in src_text, "")

    # 字幕避让控制栏（第 27 轮修复）
    check("字幕抬高避开控制栏", 'sub-pos' in src_text, "")
    check("字幕底部留边距", 'sub-margin-y' in src_text, "")


def check_data_files(src_text, doc_text):
    """文档「数据存放位置」列出的文件，代码里必须真的会生成。"""
    # 从文档表格里提取 .dat 文件名
    listed = set(re.findall(r"([a-z_]+\.dat)", doc_text))
    for f in sorted(listed):
        # 代码里通常会拼接文件名
        ok = f in src_text
        check(f"数据文件在代码中出现：{f}", ok, "")
    # 反向检查：代码里写的 .dat 是否都在文档里
    used = set(re.findall(r'"([a-z_]+\.dat)"', src_text))
    missing = used - listed
    check("代码生成的所有 .dat 都已列入文档",
          not missing, f"文档缺少：{sorted(missing)}" if missing else "齐全")


def check_readme_numbers(src_text):
    """README 里的数值声明也要与代码一致。

    教训（第 40 轮）：README 曾写「支持 0.5x ~ 3.0x 倍速」，
    而代码是 {1.0, 1.25, 1.5, 2.0, 0.75}（0.75x ~ 2.0x）。
    check_docs.py 此前只读 docs/使用指南.md，完全没看 README。
    """
    readme = ROOT / "README.md"
    if not readme.exists():
        check("README 存在", False, "")
        return
    text = readme.read_text(encoding="utf-8")

    # 倍速范围
    m = re.search(r"speeds\[\]\s*=\s*\{([^}]+)\}", src_text)
    if m:
        vals = [float(x.strip()) for x in m.group(1).split(",")]
        lo, hi = min(vals), max(vals)
        # README 里若出现 "0.5x ~ 3.0x" 这类范围，必须与代码一致
        rng = re.search(r"([0-9.]+)x\s*[~～-]\s*([0-9.]+)x", text)
        if rng:
            r_lo, r_hi = float(rng.group(1)), float(rng.group(2))
            check("README 倍速范围与代码一致",
                  abs(r_lo - lo) < 1e-6 and abs(r_hi - hi) < 1e-6,
                  f"README {r_lo}~{r_hi}x / 代码 {lo}~{hi}x")
        else:
            # README 若改用「显式序列」写法，则逐处核对。
            # 同一份 README 里可能多处提到倍速（特性列表 + 按键表），
            # 每一处都必须与代码一致——不能只看第一处。
            seqs = re.findall(r"([0-9.]+x(?:\s*(?:→|->)\s*[0-9.]+x)+)", text)
            ok = True
            detail = ""
            for one in seqs:
                got = [float(x) for x in re.findall(r"([0-9.]+)x", one)]
                if got != vals:
                    ok = False
                    detail = f"README {got} / 代码 {vals}"
                    break
            if seqs:
                check(f"README 倍速序列与代码一致（{len(seqs)} 处）", ok,
                      detail or f"{len(seqs)} 处全部一致")

    # 数值化的功能条数声明（如「18 个按键」）不做强校验，
    # 因为 README 常用概述性语言。


def check_doc_scripts(doc_text):
    """README / 使用指南里提到的每个脚本路径都必须真实存在。"""
    import os
    readme = ROOT / "README.md"
    combined = doc_text
    if readme.exists():
        combined += "\n" + readme.read_text(encoding="utf-8")
    doc_text = combined
    seen = set()
    for m in re.finditer(r"(tests/tools/[a-z_]+\.py|scripts/[a-z-]+\.sh)", doc_text):
        seen.add(m.group(1))
    for p in sorted(seen):
        check(f"文档提到的 {p} 存在", os.path.exists(p), "")


def check_env_vars(doc_text, src_text):
    """文档提到的每个 DECKPLAYER_* 环境变量都必须被代码读取。"""
    doc_vars = set(re.findall(r"DECKPLAYER_[A-Z_]+", doc_text))
    for v in sorted(doc_vars):
        # 排除纯测试用途的
        if v in ("DECKPLAYER_RESOURCES", "DECKPLAYER_WINDOW", "DECKPLAYER_FPS"):
            check(f"环境变量 {v} 被代码读取", v in src_text, "")
        else:
            check(f"环境变量 {v} 被代码读取", v in src_text, "")


def check_resources_var(doc_text):
    """文档提到的 DECKPLAYER_RESOURCES 必须被代码支持。"""
    ok = "DECKPLAYER_RESOURCES" in doc_text
    if not ok:
        check("文档提到 DECKPLAYER_RESOURCES", False, "")
        return
    src = (ROOT / "src" / "utils" / "resource_locator.cpp").read_text(
        encoding="utf-8", errors="replace")
    check("DECKPLAYER_RESOURCES 被代码读取",
          "DECKPLAYER_RESOURCES" in src, "")


def main():
    ap = argparse.ArgumentParser(description="核对用户文档与实现是否一致")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not DOC.exists():
        print(f"找不到 {DOC}")
        return 2

    doc_text = DOC.read_text(encoding="utf-8")
    src_text = read_all_sources()

    print("\033[36m══ 界面文案 ══\033[0m")
    check_ui_strings(src_text, args.verbose)

    print("\033[36m══ 手柄按键 ══\033[0m")
    check_keybindings(src_text, args.verbose)

    print("\033[36m══ 行为参数 ══\033[0m")
    check_behaviors(src_text, doc_text, args.verbose)

    print("\033[36m══ 数据文件 ══\033[0m")
    check_data_files(src_text, doc_text)

    print("\033[36m══ 环境变量 ══\033[0m")
    check_env_vars(doc_text, src_text)
    check_doc_scripts(doc_text)
    check_readme_numbers(src_text)
    check_resources_var(doc_text)

    print("\n" + "─" * 56)
    failed = [r for r in RESULTS if not r[1]]
    for name, ok, detail in RESULTS:
        mark = "\033[32m✓\033[0m" if ok else "\033[31m✗\033[0m"
        print(f" {mark} {name}" + (f"  \033[90m({detail})\033[0m" if detail else ""))
    print("─" * 56)
    if failed:
        print(f"\033[31m不一致 {len(failed)} / {len(RESULTS)}\033[0m")
        return 1
    print(f"\033[32m文档与实现一致（{len(RESULTS)} 项）\033[0m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
