#!/usr/bin/env python3
"""产物级许可证审计。

回答一个具体问题：**这个已经编译出来的二进制，能不能闭源分发？**

与 `cmake/license_probe.cpp` 的区别：

| 检查 | 时机 | 检查对象 |
| :--- | :--- | :--- |
| `license_probe.cpp` | 构建期 | 构建机上的 FFmpeg |
| 本脚本 | 构建后 | **产物实际依赖的那个 FFmpeg** |

两者都需要。构建期探测只能证明「编译时看到的是干净的 FFmpeg」，
但如果运行时加载的是另一个 libmpv（例如 Homebrew 覆盖、PATH 顺序、
`DYLD_LIBRARY_PATH` 干扰），产物仍可能实际链到 GPL 版本。
本脚本顺着动态库依赖链走到底，逐个调 `avutil_license()` 判定。

用法：
    python3 tests/tools/license_audit.py                     # 审计 build/muvik
    python3 tests/tools/license_audit.py --binary /path/to/x
    python3 tests/tools/license_audit.py --strict            # GPL 视为失败（发布模式）
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))


def section(t):
    print(f"\n\033[36m══ {t} ══\033[0m")


def deps(binary):
    """列出二进制直接依赖的动态库（macOS otool / Linux ldd）。"""
    if sys.platform == "darwin":
        r = subprocess.run(["otool", "-L", str(binary)],
                           capture_output=True, text=True)
        lines = r.stdout.splitlines()[1:]
        out = []
        for ln in lines:
            ln = ln.strip()
            if not ln:
                continue
            out.append(ln.split(" (")[0].replace("\t", ""))
        return out
    else:
        r = subprocess.run(["ldd", str(binary)],
                           capture_output=True, text=True)
        out = []
        for ln in r.stdout.splitlines():
            ln = ln.strip()
            if "=>" in ln:
                tgt = ln.split("=>")[1].strip().split()[0]
            else:
                tgt = ln.split()[0]
            if tgt.startswith("/"):
                out.append(tgt)
        return out


def resolve_closure(binary, limit=400):
    """递归展开动态库依赖，返回所有实际存在的库路径。"""
    seen = set()
    queue = [Path(binary)]
    while queue and len(seen) < limit:
        cur = queue.pop()
        for d in deps(cur):
            p = Path(d)
            if p in seen:
                continue
            seen.add(p)
            # @rpath / @loader_path 之类的先跳过（无法直接解析）
            if p.is_file():
                queue.append(p)
    return seen


def avutil_license(lib):
    """调用库里的 avutil_license()。用 ctypes 直接取符号，跨平台。"""
    import ctypes
    try:
        libc = ctypes.CDLL(str(lib))
        fn = libc.avutil_license
        fn.restype = ctypes.c_char_p
        fn.argtypes = []
        raw = fn()
        return raw.decode("utf-8", "replace") if raw else "(null)"
    except (OSError, AttributeError) as e:
        return f"<无法调用: {e}>"


def main():
    default_bin = str(ROOT / "build" / "muvik" if (ROOT / "build" / "muvik").exists() else ROOT / "build" / "muvik")
    ap = argparse.ArgumentParser(description="产物级许可证审计")
    ap.add_argument("--binary", default=default_bin)
    ap.add_argument("--strict", action="store_true",
                    help="GPL 视为失败（发布模式）；默认仅警告")
    args = ap.parse_args()

    binary = Path(args.binary)
    section("产物")
    check("二进制存在", binary.is_file(), str(binary))
    if not binary.is_file():
        report(args.strict)
        return 1

    # ── 1. 直接依赖 ─────────────────────────────────────────
    section("直接依赖")
    direct = deps(binary)
    mpv = [d for d in direct if "libmpv" in d.lower()]
    check("链接了 libmpv", bool(mpv), mpv[0] if mpv else "未找到")

    # LGPL 要求：必须是动态链接，不能静态编进来
    if mpv:
        check("libmpv 为动态链接（LGPL 可替换要求）",
              mpv[0].endswith((".dylib", ".so")) or ".so." in mpv[0],
              mpv[0].split("/")[-1])

    # 确认 mpv 的符号确实是外部导入，而非静态编入
    try:
        if sys.platform == "darwin":
            r = subprocess.run(["nm", "-u", str(binary)],
                               capture_output=True, text=True)
            undef = r.stdout
        else:
            r = subprocess.run(["nm", "-D", "-u", str(binary)],
                               capture_output=True, text=True)
            undef = r.stdout
        check("mpv_* 符号为未定义（来自动态库，未静态链接）",
              "mpv_create" in undef,
              "找到 mpv_create 的未定义引用" if "mpv_create" in undef
              else "未找到，可能被静态链接了")
    except FileNotFoundError:
        check("mpv_* 符号为未定义", False, "缺少 nm 工具")

    # ── 2. 完整依赖闭包 ─────────────────────────────────────
    section("依赖闭包")
    closure = resolve_closure(binary)
    avutils = [p for p in closure if "libavutil" in p.name]
    check("依赖链上存在 libavutil", bool(avutils),
          f"{len(avutils)} 个" if avutils else "未找到")
    for p in sorted(avutils):
        check(f"avutil 路径  {p.name}", True, str(p))

    # ── 3. 逐个调用 avutil_license() ────────────────────────
    section("许可证判定")
    gpl_found = []
    for p in sorted(avutils):
        lic = avutil_license(p)
        is_gpl = "GPL" in lic.upper()
        if is_gpl:
            gpl_found.append((p, lic))
        check(f"{p.name} 许可证", True, lic)

    # ── 4. 结论 ─────────────────────────────────────────────
    section("结论")
    if gpl_found:
        detail = "; ".join(f"{p.name}: {l}" for p, l in gpl_found)
        if args.strict:
            check("产物可闭源分发", False, f"依赖 GPL：{detail}")
        else:
            check("产物可闭源分发", True,
                  f"警告：依赖 GPL（{detail}）——仅限本地自测，不可分发")
        print("\n\033[33m注意：此产物链接了 GPL 组件，不能对外分发。\033[0m")
        print("发布前必须先用 scripts/build-lgpl-libmpv.sh 产出 LGPL 版，")
        print("再用 -DMPV_ROOT=<前缀> -DCMAKE_PREFIX_PATH=<前缀> 重新构建。")
    else:
        check("产物可闭源分发", True, "依赖链上无非 GPL 组件")

    return report(args.strict, gpl_found)


def report(strict, gpl_found=None):
    print("\n" + "─" * 60)
    failed = [r for r in RESULTS if not r[1]]
    for name, ok, detail in RESULTS:
        mark = "\033[32m✓\033[0m" if ok else "\033[31m✗\033[0m"
        print(f" {mark} {name}" + (f"  \033[90m({detail})\033[0m" if detail else ""))
    print("─" * 60)
    if failed:
        print(f"\033[31m审计未通过：{len(failed)} / {len(RESULTS)}\033[0m")
        return 1
    if gpl_found:
        print(f"\033[33m审计完成（{len(RESULTS)} 项）：存在 GPL 依赖，不可分发\033[0m")
        return 0 if not strict else 1
    print(f"\033[32m审计通过（{len(RESULTS)} 项）：可闭源分发\033[0m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
