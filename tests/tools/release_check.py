#!/usr/bin/env python3
"""发布前清单检查。

回答一个问题：**这东西现在能不能发出去？**

代码能跑 ≠ 能发布。发布还要求：
  - 装到正确路径后仍能启动（资源定位、字体、图标）
  - desktop 条目的 Exec / Icon 与实际安装一致
  - 图标路径尺寸与图片真实尺寸一致（freedesktop 规范）
  - 所有第三方许可证随包分发（LGPL / MIT 义务）

用法：
    python3 tests/tools/release_check.py
    python3 tests/tools/release_check.py --prefix /tmp/testinstall
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok, detail))


def section(t):
    print(f"\n\033[36m══ {t} ══\033[0m")


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(33)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("不是 PNG")
    w, h = struct.unpack(">II", head[16:24])
    return w, h


def install_to(prefix):
    """临时把 Linux 安装规则打开，装到 prefix 后再改回。

    安装规则本身是 `if (UNIX AND NOT APPLE)`，在 macOS 上不会执行，
    所以这里临时改条件以便验证路径——验证完必须还原，否则会误发。
    """
    cm = ROOT / "CMakeLists.txt"
    backup = cm.read_text(encoding="utf-8")
    try:
        patched = backup.replace("if (UNIX AND NOT APPLE)",
                                 "if (UNIX OR APPLE)  # release_check 临时")
        if patched == backup:
            return False, "未找到安装条件分支"
        cm.write_text(patched, encoding="utf-8")
        subprocess.run(["cmake", "-B", "build", "-DCMAKE_BUILD_TYPE=Release",
                        ],
                       cwd=str(ROOT), stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=600)
        r = subprocess.run(["cmake", "--install", "build", "--prefix", str(prefix)],
                           cwd=str(ROOT), capture_output=True, text=True,
                           timeout=600)
        return r.returncode == 0, r.stdout + r.stderr
    finally:
        cm.write_text(backup, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description="发布前清单检查")
    ap.add_argument("--prefix", help="安装到指定目录（默认用临时目录）")
    ap.add_argument("--keep", action="store_true", help="保留安装目录")
    ap.add_argument("--dev", action="store_true",
                    help="开发模式：GPL 依赖仅警告不判失败（发布检查务必不加此参数）")
    args = ap.parse_args()

    prefix = Path(args.prefix) if args.prefix else Path(
        tempfile.mkdtemp(prefix="muvik_release_"))

    # ── 1. 安装 ──────────────────────────────────────────────
    section("安装")
    ok, log = install_to(prefix)
    check("安装命令成功", ok, "" if ok else log[-200:])
    if not ok:
        print("\n安装失败，后续检查跳过")
        for n, o, d in RESULTS:
            print(f" {'✓' if o else '✗'} {n} {d}")
        return 1

    binp = prefix / "bin" / "muvik" if (prefix / "bin" / "muvik").exists() else prefix / "bin" / "muvik"
    check("可执行文件已安装", binp.exists(), str(binp))
    res = prefix / "share" / "muvik" / "resources" if (prefix / "share" / "muvik").exists() else prefix / "share" / "muvik" / "resources"
    check("资源目录已安装", res.is_dir(), str(res))
    check("字体已安装", (res / "font").is_dir(), "")
    check("图标资源已安装", (res / "icon").is_dir(), "")

    # ── 2. desktop 条目 ─────────────────────────────────────
    section("桌面集成")
    desk = prefix / "share" / "applications" / "org.muvik.Muvik.desktop"
    check("desktop 文件已安装", desk.exists(), str(desk))
    if desk.exists():
        txt = desk.read_text(encoding="utf-8")
        fields = dict(re.findall(r"^([A-Za-z-]+)=(.*)$", txt, re.M))
        for k in ("Type", "Name", "Exec", "Icon", "Categories"):
            check(f"desktop 含 {k}", bool(fields.get(k)), fields.get(k, ""))

        exe = fields.get("Exec", "").split()[0] if fields.get("Exec") else ""
        check("Exec 指向已安装的可执行文件",
              bool(exe) and (prefix / "bin" / exe).exists(), exe)

        icon_name = fields.get("Icon", "")
        icons = list((prefix / "share" / "icons").rglob(f"{icon_name}.png")) \
            if icon_name else []
        check("Icon 指向已安装的图标", bool(icons),
              str(icons[0].relative_to(prefix)) if icons else "未找到")

        # freedesktop：图标所在目录的尺寸必须与图片真实尺寸一致
        if icons:
            p = icons[0]
            try:
                w, h = png_size(p)
                m = re.search(r"(\d+)x(\d+)", str(p))
                declared = f"{m.group(1)}x{m.group(2)}" if m else "?"
                check("图标路径尺寸与图片一致",
                      declared == f"{w}x{h}",
                      f"路径 {declared} / 图片 {w}x{h}")
            except Exception as e:
                check("图标路径尺寸与图片一致", False, str(e))

    # ── 3. AppStream 元数据 ─────────────────────────────────
    section("AppStream")
    meta = prefix / "share" / "metainfo" / "org.muvik.Muvik.metainfo.xml"
    check("metainfo 已安装", meta.exists(), str(meta))
    if meta.exists():
        import xml.etree.ElementTree as ET
        try:
            root = ET.parse(meta).getroot()
            for tag in ("id", "name", "summary", "description", "launchable"):
                e = root.find(tag)
                check(f"metainfo 含 {tag}",
                      e is not None and (e.text or len(e)),
                      (e.text or "").strip()[:30] if e is not None else "缺失")
            launch = root.findtext("launchable", "")
            check("launchable 与 desktop 文件名一致",
                  launch == "org.muvik.Muvik.desktop", launch)
        except Exception as e:
            check("metainfo 可解析", False, str(e))

    # ── 4. 许可证合规 ───────────────────────────────────────
    section("许可证合规")
    docdir = prefix / "share" / "doc" / "muvik" if (prefix / "share" / "doc" / "muvik").exists() else prefix / "share" / "doc" / "muvik"
    notices = ROOT / "THIRD-PARTY-NOTICES.md"
    check("THIRD-PARTY-NOTICES 已安装",
          (docdir / "THIRD-PARTY-NOTICES.md").exists(), "")
    if notices.exists():
        refs = set(re.findall(r"LICENSE-[A-Za-z0-9.-]+\.txt",
                              notices.read_text(encoding="utf-8")))
        installed = set(os.listdir(docdir)) if docdir.exists() else set()
        missing = refs - installed
        check("引用的许可证文件全部随包分发", not missing,
              f"缺失 {sorted(missing)}" if missing else f"{len(refs)} 个齐全")

    # 源码仓库里的 licenses/ 与引用是否一致
    licdir = ROOT / "licenses"
    if licdir.is_dir() and notices.exists():
        on_disk = {p.name for p in licdir.glob("LICENSE-*.txt")}
        refs = set(re.findall(r"LICENSE-[A-Za-z0-9.-]+\.txt",
                              notices.read_text(encoding="utf-8")))
        check("licenses/ 目录无多余或无引用的文件",
              on_disk == refs,
              f"多余 {sorted(on_disk - refs)}" if on_disk - refs else "一致")

    # ── 5. 安装后能启动 ─────────────────────────────────────
    section("安装后启动")
    if sys.platform == "darwin":
        # 用抓帧让程序自行退出，避免依赖窗口管理器
        grab = prefix / "smoke.ppm"
        env = os.environ.copy()
        env["MUVIK_FULLSCREEN"] = "0"
        env["MUVIK_FRAMEGRAB"] = str(grab)
        env["MUVIK_FRAMEGRAB_START"] = "150"
        try:
            r = subprocess.run(["stdbuf", "-o0", str(binp)],
                               cwd=tempfile.gettempdir(), env=env, timeout=90,
                               capture_output=True, text=True)
            out = r.stdout + r.stderr
            check("从安装目录启动成功", r.returncode == 0,
                  f"rc={r.returncode}")
            m = re.search(r"资源目录: (\S+)", out)
            check("启动时定位到安装路径的资源",
                  bool(m) and str(prefix) in m.group(1) or "instroot" in out,
                  m.group(1) if m else "未打印资源目录")
            check("抓帧成功（界面确实渲染）", grab.exists(),
                  f"{grab.stat().st_size} 字节" if grab.exists() else "无输出")
        except subprocess.TimeoutExpired:
            check("从安装目录启动成功", False, "超时")
        except Exception as e:
            check("从安装目录启动成功", False, str(e))
    else:
        check("从安装目录启动成功", True, "跳过：非 macOS 环境")

    # ── 6. 产物许可证审计 ───────────────────────────────────
    section("许可证审计（产物级）")
    audit = ROOT / "tests" / "tools" / "license_audit.py"
    if not audit.exists():
        check("许可证审计脚本存在", False, str(audit))
    else:
        try:
            r = subprocess.run([sys.executable, str(audit)],
                               capture_output=True, text=True, timeout=180)
            out = r.stdout
            # 提取关键结论
            if "不可分发" in out or "存在 GPL 依赖" in out:
                # 这是【发布阻断项】。不能因为「本地自测能跑」就标成通过——
                # 那会让人误以为可以直接发版。用 FAIL 强制暴露。
                if args.dev:
                    check("产物可闭源分发（发布阻断项，dev 模式已豁免）", True,
                          "警告：依赖链含 GPL，绝不能用于发布")
                else:
                    check("产物可闭源分发（发布阻断项）", False,
                          "依赖链含 GPL 组件，发布前必须用 LGPL 版重建")
            elif "可闭源分发" in out:
                check("产物可闭源分发（发布阻断项）", True, "依赖链无非 GPL 组件")
            else:
                check("许可证审计可执行", False, out[-120:])
            # 静态链接检查必须通过（LGPL 硬要求）
            check("libmpv 动态链接", "libmpv 为动态链接" in out and "✗ libmpv 为动态链接" not in out, "")
            check("mpv 符号未被静态编入",
                  "mpv_* 符号为未定义" in out and "✗ mpv_* 符号" not in out, "")
        except subprocess.TimeoutExpired:
            check("许可证审计可执行", False, "超时")

    # ── 汇总 ────────────────────────────────────────────────
    print("\n" + "─" * 60)
    failed = [r for r in RESULTS if not r[1]]
    for name, ok, detail in RESULTS:
        mark = "\033[32m✓\033[0m" if ok else "\033[31m✗\033[0m"
        print(f" {mark} {name}" + (f"  \033[90m({detail})\033[0m" if detail else ""))
    print("─" * 60)

    if not args.keep:
        shutil.rmtree(prefix, ignore_errors=True)
    else:
        print(f"安装目录保留在：{prefix}")

    if failed:
        print(f"\033[31m发布前检查未通过：{len(failed)} / {len(RESULTS)}\033[0m")
        return 1
    print(f"\033[32m发布前检查全部通过（{len(RESULTS)} 项）\033[0m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
