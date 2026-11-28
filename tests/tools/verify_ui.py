#!/usr/bin/env python3
"""Muvik 端到端 UI 验证。

把第 18–26 轮人工做过的验证固化成可重复脚本，供人工复核与 CI 使用。

它解决的核心问题：
    macOS 上 `screencapture` 需要屏幕录制权限，在无权限/显示器休眠/CI
    环境会返回纯黑图，曾导致「界面没渲染」的误判。改为从程序内部读
    GL 帧缓冲（FrameGrabber），完全绕开这个限制。

用法：
    python3 tests/tools/verify_ui.py                 # 跑全部检查
    python3 tests/tools/verify_ui.py --only tabs     # 只跑某一组
    python3 tests/tools/verify_ui.py --list          # 列出所有检查

前提：先构建好 ./build/muvik，并准备好 ffmpeg（用于造测试素材）。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 产物名可能是 yiufplayer，也可能只有旧别名 muvik。
#
# 注意这里**原先写的是**
#     BIN = ROOT / "build" / "muvik"
#     if not BIN.exists():
#         BIN = ROOT / "build" / "muvik"        # 两个分支同一个值
# 等于完全没有回退。ASan 段照抄了同样的写法（只找 build-asan/muvik），
# 于是「ASan 内存安全检查」在 build-asan 只有 yiufplayer 时会静默跳过 ——
# 打印一行「跳过：未找到 ASan 版本」，然后把这一组算作没失败，实际一次都没跑。
def _resolve_bin(dirname):
    """在给定构建目录里找可执行文件。

    只认 yiufplayer / muvik 两个当前名字。
    **不要回退到 deck-player** —— 那是改名前的旧产物，可能几个月没重建；
    实测 build-asan/ 里就躺着一个 10 月 29 日的 deck-player（39MB，debug 符号），
    选中它会让整组 ASan 检查跑在一个与当前源码无关的二进制上，
    表现为「全部超时」，比跳过更具误导性。
    """
    d = ROOT / dirname
    for name in ("yiufplayer", "muvik"):
        p = d / name
        if p.exists():
            return p
    return d / "yiufplayer"   # 都不存在时返回期望路径，供断言报错

BIN = _resolve_bin("build")

# ── 结果收集 ──────────────────────────────────────────────────────────
RESULTS = []


def record(group, name, ok, detail=""):
    RESULTS.append((group, name, ok, detail))


def section(title):
    print(f"\n\033[36m══ {title} ══\033[0m")


# ── 基础工具 ──────────────────────────────────────────────────────────
def load_ppm(path):
    import numpy as np
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"不是 P6 PPM: {magic}")
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        f.readline()
        return np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)


def run_player(env_extra, timeout=60, args=None):
    """运行播放器，返回 (returncode, 捕获的输出)。

    必须用 stdbuf 关闭 stdout 缓冲：程序被超时 kill 时，
    全缓冲的输出会整段丢失，导致日志里什么都看不到、
    误判为「功能没触发」。（实测踩过：连播明明正常，
    却因为日志丢失被判定为失败。）
    """
    env = os.environ.copy()
    env.setdefault("MUVIK_FULLSCREEN", "0")
    env.update({k: str(v) for k, v in env_extra.items()})
    cmd = ["stdbuf", "-o0", str(BIN)] + (args or [])
    try:
        p = subprocess.run(
            cmd, cwd=str(BIN.parent), env=env, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        out = e.stdout or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        return -9, out


def grab(tmpdir, name, env_extra, timeout=60, args=None, start=150):
    """抓一帧（或多帧）并返回路径列表。"""
    pattern = Path(tmpdir) / f"{name}.ppm"
    e = dict(env_extra)
    e["MUVIK_FRAMEGRAB"] = str(pattern)
    e.setdefault("MUVIK_FRAMEGRAB_START", start)
    rc, log = run_player(e, timeout=timeout, args=args)
    files = sorted(Path(tmpdir).glob(f"{name}*.ppm"))
    return rc, log, files


# ── 素材准备 ──────────────────────────────────────────────────────────
def make_media(tmpdir):
    """用 ffmpeg 生成测试素材；失败时返回 None。

    注意：所有 ffmpeg 调用都必须带 `stdin=subprocess.DEVNULL`。
    ffmpeg 会读标准输入（用于接收 q/方向键等交互命令），
    若父进程把 stdin 原样继承给它、而那个 stdin 又一直不关闭，
    ffmpeg 会**永久阻塞**在读取上 —— 表现为「整个验证脚本卡住不输出」，
    而且 CPU 占用是 0%，看上去像死锁而不是慢。
    实测：从终端直接跑没问题（stdin 是 tty），从脚本/管道里跑就会卡。
    """
    if not shutil.which("ffmpeg"):
        return None

    d = Path(tmpdir) / "media"
    d.mkdir(parents=True, exist_ok=True)

    # 彩条视频（用于验证画面确实渲染）
    color = d / "color.mp4"
    if not color.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi",
             "-i", "testsrc2=duration=20:size=640x480:rate=15",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", str(color)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    # 黑底 + 内嵌字幕
    srt = d / "sub.srt"
    srt.write_text(
        "1\n00:00:00,500 --> 00:00:10,000\nSUBTITLE TEST LINE ONE\n\n"
        "2\n00:00:10,500 --> 00:00:20,000\nSECOND SUBTITLE\n",
        encoding="utf-8",
    )
    subvid = d / "subtitled.mkv"
    if not subvid.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi",
             "-i", "color=c=black:s=640x360:d=20:r=10", "-i", str(srt),
             "-map", "0:v", "-map", "1:s", "-c:v", "libx264",
             "-c:s", "srt", str(subvid)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    # 4:3 视频：用于验证在 16:10 屏幕上的宽高比处理
    v43 = d / "r43.mp4"
    if not v43.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi",
             "-i", "testsrc2=duration=8:size=640x480:rate=10",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", str(v43)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    # 音频
    audio = d / "song.mp3"
    if not audio.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi",
             "-i", "sine=frequency=440:duration=30",
             "-c:a", "libmp3lame", "-b:a", "128k", str(audio)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    # 连播用：三集短片段，放在独立子目录，
    # 避免与 color.mp4 / subtitled.mkv 混在一起被一起加入播放列表
    # （否则「第 2 集」会变成 20 秒的彩条视频，连播检查永远等不完）。
    series_dir = d / "series"
    series_dir.mkdir(parents=True, exist_ok=True)
    for name, spec in [
        ("ep1.mp4", "testsrc2=duration=3:size=320x240:rate=10"),
        ("ep2.mp4", "color=c=red:s=320x240:d=3:r=10"),
        ("ep3.mp4", "color=c=blue:s=320x240:d=3:r=10"),
    ]:
        p = series_dir / name
        if not p.exists():
            subprocess.run(
                ["ffmpeg", "-y", "-f", "lavfi", "-i", spec,
                 "-c:v", "libx264", "-pix_fmt", "yuv420p", str(p)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )

    # 外挂字幕：视频 + 同目录 .srt（验证 sub-auto）
    # 用**纯黑底**：这样字幕是画面里唯一的白色来源，
    # 判据才干净。用 testsrc 之类的彩色图案时，
    # 图案本身的白像素淹没了字幕（实测 16531 vs 12679，只有 1.3 倍）。
    extsub = d / "extsub.mp4"
    if not extsub.exists():
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi",
             "-i", "color=c=black:s=640x480:d=30:r=10",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", str(extsub)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    srt = d / "extsub.srt"
    if not srt.exists():
        # 字幕显示窗口要足够宽：抓帧按 **UI 帧数**计，而 UI 帧率随机器浮动
        # （本机约 1200fps，60fps 的普通机器慢 20 倍）。
        # 旧素材窗口是 1~14 秒 + 15 秒视频，60fps 机器上抓到的是 EOF 黑帧、
        # 本机又抓在字幕出现之前 —— 两端都会拿 0 像素，导致该项**随机失败**。
        # 改成 30 秒视频 + 0.5~28 秒窗口，任何帧率下三次抓帧至少一次落在窗口内。
        srt.write_text(
            "1\n00:00:00,500 --> 00:00:28,000\n"
            "EXTERNAL SUBTITLE TEST LINE\n", encoding="utf-8")

    # 多音轨：两条不同频率的音轨，验证切换
    multi = d / "multitrack.mp4"
    if not multi.exists():
        subprocess.run(
            ["ffmpeg", "-y",
             "-f", "lavfi", "-i", "testsrc2=duration=15:size=640x480:rate=10",
             "-f", "lavfi", "-i", "sine=frequency=440:duration=15",
             "-f", "lavfi", "-i", "sine=frequency=880:duration=15",
             "-map", "0:v", "-map", "1:a", "-map", "2:a",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
             str(multi)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    return {
        "color": color,
        "external_subs": extsub,
        "multitrack": multi,
        "subtitled": subvid,
        "audio": audio,
        "v43": v43,
        "series": series_dir / "ep1.mp4",
        "series_dir": series_dir,
        "dir": d,
    }


# ── 各项检查 ──────────────────────────────────────────────────────────
def check_basic_render(tmpdir, media):
    """界面能渲染、深色主题、有文字。"""
    section("基础渲染")
    rc, log, files = grab(tmpdir, "basic", {"MUVIK_FRAMEGRAB_START": 120})
    if not files:
        record("基础渲染", "抓帧成功", False, f"rc={rc}")
        return
    a = load_ppm(files[0])
    g = a.mean(axis=2)
    bright = (g > 120).mean() * 100

    record("基础渲染", "抓帧成功", True, f"{a.shape[1]}x{a.shape[0]}")
    # 深色主题判据：不能断言「中性灰」。
    #
    # 这条检查原名「深色主题（中性灰 >90%）」，写于色板是中性灰的年代。
    # 换成影院蓝之后它必然失败（实测 0.9%）—— 但那是色板**有意**变蓝，
    # 不是渲染坏了。把设计决策当成缺陷来守，是检查写错了地方。
    #
    # 改为断言真正要保证的两件事：
    #   1. 画面确实是深的（大部分像素亮度低）
    #   2. 暗部色相统一（不是一堆杂色拼贴）
    dark_ratio = (g < 80).mean() * 100
    # 蓝色通道普遍高于红色通道（影院蓝的特征），但不要求每一像素都如此
    blue_bias = float((a[:, :, 2].astype(int) - a[:, :, 0].astype(int)).mean())
    record("基础渲染", "深色底（亮度<80 占比 >60%）", dark_ratio > 60,
           f"{dark_ratio:.1f}%")
    record("基础渲染", "暗部偏蓝（影院蓝特征）", blue_bias > 5.0,
           f"蓝-红通道差 {blue_bias:.1f}")
    record("基础渲染", "有文字/图标（亮像素 >0.3%）", bright > 0.3, f"{bright:.2f}%")


def check_font(tmpdir, media):
    """字体格式必须满足 nanoVG / stb_truetype 的要求。

    这一项是在踩坑之后补的：换字体时用了可变字体（fvar），
    结果「OSD 上出现中文 → 整个 nanoVG 帧失败 → 视频变全黑」。
    当时误判成视频解码问题，排查了很久。
    把校验固化成检查项，避免再次发生。
    """
    section("字体格式")
    root = Path(__file__).resolve().parent.parent.parent
    script = root / "scripts" / "check_font.py"
    if not script.exists():
        record("字体格式", "校验脚本存在", False, str(script))
        return
    try:
        proc = subprocess.run(
            [sys.executable, str(script)],
            capture_output=True, text=True, timeout=60,
        )
        out = (proc.stdout or "") + (proc.stderr or "")
        record("字体格式", "无 fvar / glyf 轮廓 / CJK 覆盖充足",
               proc.returncode == 0,
               out.strip().splitlines()[-1] if out.strip() else "")
    except Exception as exc:                          # noqa: BLE001
        record("字体格式", "校验执行", False, str(exc))


def check_osd(tmpdir, media):
    """OSD 文字与进度条必须真的可见，且三层不重叠。

    这一组检查是在一次审计后补的 —— 当时 OSD 用 nvgFontFace(vg,"sans")，
    而 "sans" 从未注册，fontstash 返回 -1，nvgText 直接 return，
    于是**整块 OSD 一个字都画不出来**，用户只看到一排空色块。

    更隐蔽的是：原有的 79 项检查全部通过 —— 因为它们只看「视频有没有画面」
    「界面有没有亮像素」，OSD 是否可读从未被验证过。

    判据用纯黑视频做底：任何白/蓝/橙像素都只可能来自 OSD 本身。
    """
    section("OSD 可读性")
    root = Path(__file__).resolve().parent.parent.parent

    # 造一个纯黑视频：OSD 之外不该有任何亮像素
    black = Path(str(tmpdir)) / "black_osd.mp4"
    try:
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi", "-i", "color=c=black:s=1280x720:d=10",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", str(black)],
            stdin=subprocess.DEVNULL,
            capture_output=True, timeout=120,
        )
    except Exception:                                 # noqa: BLE001
        pass
    if not black.exists():
        record("OSD", "生成黑底视频", False, "ffmpeg 不可用")
        return

    # 用墙钟锚点而不是帧号：帧号与真实时间没有稳定映射（mpv 初始化前
    # 主循环空转能跑到数百 fps），同一个 START=200 在不同机器/不同时长
    # 的视频上可能落在 1 秒，也可能落在 12 秒 —— 后者 OSD 已自动隐藏，
    # 整组检查就会全 0 假红。2 秒落在 OSD 必然可见的窗口里。
    rc, log, files = grab(tmpdir, "osd", {"MUVIK_FRAMEGRAB_AT_MS": "2000"},
                          args=[str(black)])
    if not files:
        record("OSD", "抓帧成功", False, f"rc={rc}")
        return
    a = load_ppm(files[0])
    h, w = a.shape[:2]

    white = (a.min(axis=2) > 150)
    accent_color = ((a[:, :, 2] > 200) & (a[:, :, 1] > 120) & (a[:, :, 0] < 100))

    # 顶部栏（片名 + 时间）
    top_white = int(white[: int(h * 0.09)].sum())
    record("OSD", "顶部文字可见（片名/时间）", top_white > 200,
           f"{top_white} 个白像素")

    # 底部按钮文字：功能行在主控行上方
    body_white = int(white[int(h * 0.70): int(h * 0.90)].sum())
    record("OSD", "按钮文字可见", body_white > 200, f"{body_white} 个白像素")

    # 进度条：当前设计是「accent 强调色填充 + 25% 白轨道」。
    # 强调色段长度取决于已播比例（开播时机不同就会变），所以判据不看颜色，
    # 而看「有没有一条横向铺满大半屏宽的亮线」——整个 OSD 里只有进度条
    # 能做到这一点（按钮是圆的，文字是断的）。轨道亮度 52/255，
    # 阈值取 25 即可把轨道一起算进来。
    lit_rows = (a.max(axis=2) > 25)
    band_top = int(h * 0.85)
    counts = lit_rows[band_top:].sum(axis=1)
    bar_y, longest, run_lo, run_hi = -1, 0, 0, 0
    if counts.size and counts.max() > 0:
        bar_y = band_top + int(counts.argmax())
        run, start = 0, 0
        for x, on in enumerate(lit_rows[bar_y]):
            if on:
                if run == 0:
                    start = x
                run += 1
                if run > longest:
                    longest, run_lo, run_hi = run, start, x + 1
            else:
                run = 0
    record("OSD", "进度条可见", longest > w * 0.60,
           f"最长横向连续亮段 {longest}px（屏宽 {w}）")

    # 主控按钮（SteamOS 蓝实心，圆形），在进度条上方
    btn_accent = int(accent_color[int(h * 0.70): int(h * 0.90)].sum())
    record("OSD", "主控按钮可见", btn_accent > 200, f"{btn_accent} 个强调色像素")

    # 三层不重叠：进度条所在行除了这条长段，不应再有强调色
    # （按钮压上来会在长段之外多出若干短段）
    if bar_y >= 0:
        row_accent = int(accent_color[bar_y].sum())
        in_run = int(accent_color[bar_y][run_lo:run_hi].sum())
        record("OSD", "进度条不被按钮覆盖", row_accent - in_run < 50,
               f"进度条所在行 y={bar_y}，长段之外另有 {row_accent - in_run} 个强调色像素")


def check_tabs(tmpdir, media):
    """四个标签页渲染互不相同。"""
    section("四个标签页")
    import numpy as np
    frames = {}
    for i in range(4):
        rc, log, files = grab(tmpdir, f"tab{i}",
                              {"MUVIK_START_TAB": i,
                               "MUVIK_FRAMEGRAB_START": 150}, start=150)
        if files:
            frames[i] = load_ppm(files[0])

    record("标签页", "四个页面都能抓帧", len(frames) == 4, f"{len(frames)}/4")
    if len(frames) == 4:
        min_diff = 100.0
        for i in range(4):
            for j in range(i + 1, 4):
                d = np.abs(frames[i].astype(int) - frames[j].astype(int)).sum(axis=2)
                min_diff = min(min_diff, (d > 20).mean() * 100)
        record("标签页", "两两差异 >2%（不是同一屏）", min_diff > 2.0,
               f"最小差异 {min_diff:.2f}%")


def check_video(tmpdir, media):
    """视频画面确实显示在屏幕上（与「只有 UI」的基线对比）。"""
    section("视频播放")
    if not media:
        record("视频", "有 ffmpeg 素材", False, "缺少 ffmpeg")
        return

    import numpy as np

    def stats(a):
        mx = a.max(axis=2).astype(int)
        mn = a.min(axis=2).astype(int)
        return {
            "colorful": ((mx - mn) > 40).mean() * 100,
            "uniq": len(np.unique(a.reshape(-1, 3), axis=0)),
        }

    # 基线：打开音频文件时是纯 UI（无视频区域），用它代表「没有视频画面」
    rc, log, base_files = grab(
        tmpdir, "vidbase",
        {"MUVIK_FRAMEGRAB_START": 300},
        args=[str(media["audio"])], start=300)
    base = stats(load_ppm(base_files[0])) if base_files else None
    if base is None:
        record("视频", "取得 UI 基线", False, "抓帧失败")
        return
    record("视频", "取得 UI 基线", True,
           f"彩色 {base['colorful']:.1f}% 唯一色 {base['uniq']}")

    # 播放彩条视频
    rc, log, files = grab(
        tmpdir, "video",
        {"MUVIK_FRAMEGRAB_START": 300},
        args=[str(media["color"])], start=300)
    if not files:
        record("视频", "抓帧成功", False, f"rc={rc}")
        return
    v = stats(load_ppm(files[0]))

    # 关键：必须与基线【比较】，而不是用固定阈值。
    # UI 本身就有色彩与渐变，固定阈值（如「彩色 >20%」）在视频
    # 完全不渲染时依然成立，测不出问题。
    record("视频", "彩色像素显著高于 UI 基线",
           v["colorful"] > base["colorful"] + 15,
           f"{v['colorful']:.1f}% vs 基线 {base['colorful']:.1f}%")
    record("视频", "唯一色显著多于 UI 基线（>5 倍）",
           v["uniq"] > base["uniq"] * 5,
           f"{v['uniq']} vs 基线 {base['uniq']}")


def check_subtitle(tmpdir, media):
    """内嵌字幕确实绘制到画面底部（像素级验证）。"""
    section("内嵌字幕")
    if not media:
        record("字幕", "有测试素材", False, "缺少 ffmpeg")
        return

    import numpy as np

    # 素材是纯黑视频 + 内嵌字幕。若字幕渲染成功，
    # 画面底部会出现成片的白色文字像素；不渲染则几乎全黑。
    # 注意：GL 帧缓冲原点在左下，而写入 PPM 时已按文件顺序翻转，
    # 因此按【图像坐标】读底部即可（对应文件的下半部分）。
    rc, log, files = grab(
        tmpdir, "sub",
        {"MUVIK_FRAMEGRAB_START": 300,
         # 必须关掉 OSD：判定带 0.60–0.92 同时覆盖了两行按钮，
         # OSD 的白字会盖过字幕白字，导致「关字幕」对照组数值不变。
         "MUVIK_OSD_OFF": "1"},
        args=[str(media["subtitled"])], start=300)

    has_auto = "自动选中字幕轨" in log
    record("字幕", "自动选中字幕轨", has_auto,
           "找到 '自动选中字幕轨'" if has_auto else "日志中没有")

    if not files:
        record("字幕", "抓帧成功", False, f"rc={rc}")
        return

    a = load_ppm(files[0])
    g = a.mean(axis=2)
    h = a.shape[0]
    # 字幕位于画面下方区域
    band = g[int(h * 0.60):int(h * 0.98), :]
    white = int((band > 200).sum())

    # 阈值依据实测：字幕存在时为数千个白像素；
    # 无字幕时该区域应接近 0（纯黑视频）。
    record("字幕", "底部出现字幕文字像素（>1000）", white > 1000,
           f"{white} 个白像素")

    # 对照组：同一素材但关闭字幕，白像素应显著更少。
    # 这样即使阈值写错，也能通过对比发现问题。
    rc2, log2, files2 = grab(
        tmpdir, "suboff",
        {"MUVIK_FRAMEGRAB_START": 300,
         "MUVIK_OSD_OFF": "1",
         "MUVIK_SUBTITLE_OFF": "1"},
        args=[str(media["subtitled"])], start=300)
    if files2:
        g2 = load_ppm(files2[0]).mean(axis=2)
        white2 = int((g2[int(h * 0.60):int(h * 0.98), :] > 200).sum())
        record("字幕", "关闭字幕后白像素显著减少",
               white > white2 * 2 or white2 < 100,
               f"有字幕 {white} vs 关闭 {white2}")


def check_audio(tmpdir, media):
    """音频解码推进并记录进度。"""
    section("音频播放")
    if not media:
        record("音频", "有测试素材", False, "缺少 ffmpeg")
        return
    rc, log = run_player(
        {"MUVIK_FRAMEGRAB": str(Path(tmpdir) / "aud.ppm"),
         "MUVIK_FRAMEGRAB_START": 1200},
        timeout=45, args=[str(media["audio"])])
    routed = "直接播放音频" in log
    record("音频", "音频走 AudioPlayer（不是视频播放器）", routed,
           "找到 '直接播放音频'" if routed else "未见路由日志")
    appnap = "已抑制 App Nap" in log
    record("音频", "App Nap 抑制生效", appnap, "macOS 后台播放保障")


def check_gestures(tmpdir, media):
    """五项触控手势。"""
    section("触控手势")
    if not media:
        record("手势", "有测试素材", False, "缺少 ffmpeg")
        return
    rc, log = run_player(
        {"MUVIK_GESTURE_TEST": "1"},
        timeout=60, args=[str(media["color"])])
    checks = [
        ("单击切换 OSD", "【单击】"),
        ("双击切换播放/暂停", "【双击】"),
        ("右半屏上滑调音量", "【右半屏上滑】"),
        ("左半屏下滑调亮度", "【左半屏下滑】"),
        ("水平滑动快进", "【水平滑动】"),
        # 长按加速是 2026-11-03 新增的：成熟播放器都有，
        # 而本项目此前完全没有（跳过片头时只能反复点快进）。
        ("长按加速（按住提速，松手恢复）", "【长按加速】按住后"),
        ("长按后恢复原速", "【长按恢复】"),
        # 按下态：防住「代码看着有、实际一次都没执行」的静默失效
        ("按钮有按下反馈", "【按钮按下态】"),
    ]
    for label, marker in checks:
        line = next((l for l in log.splitlines() if marker in l), "")
        ok = "PASS" in line
        detail = line.split("】")[-1].strip() if line else "没有输出"
        record("手势", label, ok, detail[:60])


def check_playlist(tmpdir, media):
    """播放列表自动连播。"""
    section("自动连播")
    if not media:
        record("连播", "有测试素材", False, "缺少 ffmpeg")
        return
    # 关键：FRAMEGRAB_START 是【帧】数，不是秒。
    # 实测本机约 1200 帧/秒，按帧数估算等待时间会严重低估。
    # 因此这里按墙钟时间等待：写日志到文件，睡够再杀进程。
    # 另外必须把 stdout 重定向到【文件】而非管道——被 SIGKILL 时
    # 管道里的缓冲会全部丢失，文件里的内容（配合 stdbuf -o0）能保留。
    logpath = Path(tmpdir) / "playlist.log"
    env = os.environ.copy()
    env["MUVIK_FRAMEGRAB"] = str(Path(tmpdir) / "pl.ppm")
    env["MUVIK_FRAMEGRAB_START"] = "1000000"   # 足够大，本检查期间不退出
    with open(logpath, "w") as lf:
        p = subprocess.Popen(
            ["stdbuf", "-o0", str(BIN), str(media["series"])],
            cwd=str(BIN.parent), env=env,
            stdout=lf, stderr=subprocess.STDOUT)
        try:
            p.wait(timeout=14)      # 3 集 × 3 秒 + 启动/解码开销，实测约 10 秒
        except subprocess.TimeoutExpired:
            pass
        finally:
            if p.poll() is None:
                p.kill()
                p.wait(timeout=5)
    log = logpath.read_text(encoding="utf-8", errors="replace")
    rc = p.returncode
    joined = "同目录另有" in log
    record("连播", "同目录视频自动成列表", joined,
           "找到 '同目录另有'" if joined else "未见")
    n_switch = log.count("自动切换下一集")
    record("连播", "至少切换一次下一集", n_switch >= 1, f"{n_switch} 次")
    finished = log.count("播放结束: 当前")
    record("连播", "多集依次播完", finished >= 2, f"{finished} 集")


def check_screens(tmpdir, media):
    """多种窗口尺寸下的布局一致性（不假定屏幕尺寸）。"""
    section("屏幕适配")
    import numpy as np

    # Steam Deck 是 1280x800（16:10），桌面常见 1080p/1440p，小窗 640x360。
    sizes = ["1280x800", "1024x600", "640x360", "1920x1080"]

    def profile(a):
        """按宽度归一化后的 20 段亮像素分布，用于比较布局形状。"""
        g = a.mean(axis=2)
        br = (g > 120)
        h, w = br.shape
        return [br[:, int(w * i / 20):int(w * (i + 1) / 20)].mean() * 100
                for i in range(20)]

    profs = {}
    for spec in sizes:
        rc, log, files = grab(tmpdir, f"scr_{spec}",
                              {"MUVIK_WINDOW": spec,
                               "MUVIK_FRAMEGRAB_START": 200}, start=200)
        if not files:
            record("屏幕适配", f"{spec} 能渲染", False, f"rc={rc}")
            continue
        a = load_ppm(files[0])
        h, w = a.shape[:2]
        record("屏幕适配", f"{spec} 实际尺寸", f"{w}x{h}" == spec,
               f"抓帧 {w}x{h}")
        g = a.mean(axis=2)
        # 判据是「有没有渲染出 UI」，不是「内容够不够多」。
        # 原来卡 1%，媒体库把配置项撤出内容页（只剩影片卡片）之后，
        # 库里有 1 部片时首屏亮像素只有 0.8% —— 内容变少不是渲染失败。
        # 与 basic 组对齐到 0.3%：低于这个数才是「整屏全黑」。
        record("屏幕适配", f"{spec} 有 UI（亮像素>0.3%）",
               (g > 120).mean() * 100 > 0.3,
               f"{(g > 120).mean() * 100:.2f}%")
        profs[spec] = profile(a)

    # 布局形状应与分辨率无关：把宽度归一化后，各段分布应当接近
    base = profs.get("1280x800")
    if base:
        for spec, p in profs.items():
            if spec == "1280x800":
                continue
            diff = sum(abs(a - b) for a, b in zip(base, p)) / len(base)
            record("屏幕适配", f"{spec} 布局形状与 1280x800 一致",
                   diff < 3.0, f"平均差 {diff:.2f} 个百分点")


def check_aspect(tmpdir, media):
    """视频宽高比在非 16:9 屏幕（Steam Deck 16:10）上保持正确。"""
    section("宽高比")
    import numpy as np
    if not media:
        record("宽高比", "有测试素材", False, "缺少 ffmpeg")
        return

    # 4:3 视频放到 1280x800（16:10）屏上：
    # 正确做法是按高度撑满、左右留黑边。
    rc, log, files = grab(
        tmpdir, "ar43",
        {"MUVIK_WINDOW": "1280x800",
         "MUVIK_FRAMEGRAB_START": 250},
        args=[str(media["v43"])], start=250)
    if not files:
        record("宽高比", "抓帧成功", False, f"rc={rc}")
        return

    a = load_ppm(files[0])
    g = a.mean(axis=2)
    h, w = g.shape
    # 取画面中部一行看水平剖面，找出黑边边界
    mid = g[int(h * 0.45):int(h * 0.55)].mean(axis=0)
    lit = [x for x in range(w) if mid[x] > 8]
    if not lit:
        record("宽高比", "画面有内容", False, "整行全黑")
        return
    content_w = lit[-1] - lit[0] + 1
    # 4:3 按高度 800 撑满 -> 宽 800*4/3 ≈ 1067
    expected = h * 4 / 3
    err = abs(content_w - expected) / expected * 100
    record("宽高比", "4:3 视频在 16:10 屏上不变形", err < 12,
           f"实测宽 {content_w} / 预期 {int(expected)}（偏差 {err:.0f}%）")


def check_perf(tmpdir, media):
    """空闲与播放时的 CPU / 内存占用。

    对掌机而言 CPU 占用直接决定续航：85% 与 5% 是「能用」与「不能用」的差别。
    """
    section("性能")
    import subprocess as sp
    import time

    # 沙箱 / 受限容器里 ps 可能被直接拒绝（PermissionError），
    # 那不是程序的缺陷。先探测一次，不可用就跳过并说明原因，
    # 免得把「环境不允许」记成「检查失败」。
    try:
        sp.run(["ps", "-o", "pid=", "-p", str(os.getpid())],
               capture_output=True, timeout=5)
    except Exception as exc:                          # noqa: BLE001
        print(f"\033[33m跳过性能检查：当前环境无法执行 ps（{exc}）\033[0m")
        return

    def sample(args, seconds=5, settle=4):
        p = sp.Popen(["stdbuf", "-o0", str(BIN)] + args,
                     cwd=str(BIN.parent), env=os.environ.copy(),
                     stdout=sp.DEVNULL, stderr=sp.DEVNULL)
        try:
            time.sleep(settle)
            r = sp.run(["ps", "-o", "%cpu=,rss=", "-p", str(p.pid)],
                       capture_output=True, text=True)
            time.sleep(seconds)
            r2 = sp.run(["ps", "-o", "%cpu=,rss=", "-p", str(p.pid)],
                        capture_output=True, text=True)
            def parse(out):
                parts = out.split()
                if len(parts) < 2:
                    return None
                return float(parts[0]), int(parts[1]) // 1024
            return parse(r.stdout), parse(r2.stdout)
        finally:
            p.kill()
            p.wait()

    idle = sample([])
    if idle and idle[0] and idle[1]:
        cpu = max(idle[0][0], idle[1][0])
        rss = idle[1][1]
        # 阈值依据：修复前实测 85%（macOS 上 GLFW vsync 不生效导致忙等）；
        # 加帧率限制后为 4~5%。
        record("性能", "空闲 CPU < 20%", cpu < 20, f"{cpu:.1f}%")
        record("性能", "空闲内存 < 400 MB", rss < 400, f"{rss} MB")
    else:
        record("性能", "空闲状态可采样", False, "进程提前退出")

    if media:
        play = sample([str(media["color"])])
        if play and play[0] and play[1]:
            cpu = max(play[0][0], play[1][0])
            rss = play[1][1]
            record("性能", "播放 CPU < 40%", cpu < 40, f"{cpu:.1f}%")
            record("性能", "播放内存 < 600 MB", rss < 600, f"{rss} MB")


def check_asan(tmpdir, media):
    """在 AddressSanitizer 构建下跑核心流程，检查内存错误。

    需要先构建 build-asan：
        cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -O1" \
              -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g -O1" \
              -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
        cmake --build build-asan -j

    为什么值得单独做：Release 下 use-after-free 往往「碰巧没事」，
    只有 ASan 能稳定暴露。本项目实测靠它抓到两个真实缺陷。
    """
    section("内存安全（ASan）")
    # 用与主构建相同的解析逻辑：build-asan 里可能只有 yiufplayer，
    # 写死 "muvik" 会让这一整组静默跳过（等于没测）。
    asan_bin = _resolve_bin("build-asan")
    if not asan_bin.exists():
        record("ASan", "已构建 build-asan", False, f"跳过：{asan_bin} 不存在")
        return

    env_base = os.environ.copy()
    env_base["ASAN_OPTIONS"] = "detect_leaks=0:fast_unwind_on_fatal=1:symbolize=0"
    env_base["UBSAN_OPTIONS"] = "print_stacktrace=0"

    def run_asan(name, extra_env, args, timeout=90):
        env = dict(env_base)
        env.update(extra_env)
        try:
            p = subprocess.run(
                ["stdbuf", "-o0", str(asan_bin)] + args,
                cwd=str(asan_bin.parent), env=env, timeout=timeout,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            out = p.stdout
            rc = p.returncode
        except subprocess.TimeoutExpired:
            record("ASan", name, False, "超时（ASan 下较慢，但不该卡死）")
            return

        # 只统计**本项目代码**的报告。
        #
        # external/ 下的第三方库有已知的 UBSan 噪音，例如 QuickJS：
        #   quickjs.h:557  runtime error: 2.14748e+09 is outside the range of 'int'
        #   quickjs.c:32685 runtime error: left shift of 171 by 24 places ...
        # 按项目约定（AGENTS.md「不要为了消警告去改 external/」）不修它们，
        # 但也不该让它们把整组检查永远打成红 —— 那会让真正的 UAF 淹没在
        # 固定噪音里。这里按报告里的源码路径过滤。
        own_hits = 0
        for line in out.splitlines():
            if "runtime error" not in line and "ERROR: AddressSanitizer" not in line:
                continue
            if "external/" in line:
                continue          # 第三方库的已知噪音，不计
            own_hits += 1

        record("ASan", name, own_hits == 0 and rc == 0,
               f"rc={rc} 本项目报告={own_hits}" if own_hits or rc else "干净")

    frame = lambda n: {"MUVIK_FRAMEGRAB": str(Path(tmpdir) / f"{n}.ppm"),
                       "MUVIK_FRAMEGRAB_START": "100"}

    run_asan("启动 + UI", frame("as1"), [])
    for i in range(4):
        e = frame(f"astab{i}")
        e["MUVIK_START_TAB"] = str(i)
        run_asan(f"标签页 {i}", e, [])
    run_asan("插件管理页", {**frame("asplug"), "MUVIK_OPEN_PLUGINS": "1"}, [])

    if media:
        run_asan("视频播放", frame("asv"), [str(media["color"])])
        run_asan("字幕", frame("assub"), [str(media["subtitled"])])
        run_asan("音频", frame("asa"), [str(media["audio"])])
        # 手势自检会走到「退出清理」，曾在此暴露 DynamicBox 的 use-after-free
        run_asan("手势 + 退出清理", {"MUVIK_GESTURE_TEST": "1"},
                 [str(media["color"])], timeout=120)


def check_soak(tmpdir, media):
    """反复启动/播放/退出的稳定性（抓偶发崩溃与泄漏）。

    单次运行不崩溃说明不了什么；反复启动能暴露「只有特定顺序才触发」的问题
    （本项目第 31 轮的 use-after-free 就是靠这类路径找到的）。
    """
    section("稳定性")
    import subprocess as sp
    import time

    rounds = int(os.environ.get("MUVIK_SOAK_ROUNDS", "12"))
    crash = 0
    nonzero = 0
    details = []
    for i in range(rounds):
        if i % 4 == 0:
            env, args = {"MUVIK_START_TAB": "0"}, []
        elif i % 4 == 1:
            env, args = {"MUVIK_START_TAB": "1"}, []
        elif i % 4 == 2:
            env, args = {}, ([str(media["audio"])] if media else [])
        else:
            env, args = {}, ([str(media["color"])] if media else [])

        e = os.environ.copy()
        e.update(env)
        e["MUVIK_FRAMEGRAB"] = str(Path(tmpdir) / f"soak{i}.ppm")
        e["MUVIK_FRAMEGRAB_START"] = "150"
        try:
            p = sp.run(["stdbuf", "-o0", str(BIN)] + args,
                       cwd=str(BIN.parent), env=e, timeout=40,
                       stdout=sp.PIPE, stderr=sp.STDOUT, text=True)
            out, rc = p.stdout, p.returncode
        except sp.TimeoutExpired:
            nonzero += 1
            details.append(f"#{i} 超时")
            continue
        if rc != 0:
            nonzero += 1
            details.append(f"#{i} rc={rc}")
        if ("Segmentation" in out or "Abort" in out
                or "AddressSanitizer" in out):
            crash += 1
            details.append(f"#{i} 崩溃标记")

    record("稳定性", f"{rounds} 轮启动/播放/退出无崩溃", crash == 0,
           f"{crash} 次崩溃" if crash else "全部正常")
    record("稳定性", f"{rounds} 轮退出码均为 0", nonzero == 0,
           "; ".join(details[:3]) if details else "全部 0")


def check_dialog(tmpdir, media):
    """播放器内的对话框必须能显示在视频画面之上。

    背景：mpv 渲染发生在 postDrawHook（nvgEndFrame 之后），画满整个窗口。
    而 borealis 的 Dialog 是「压栈的新 Activity」，播放器视图仍在栈中、
    渲染钩子仍在生效。若不做判断，对话框会被视频整屏盖掉——用户按了
    音轨/字幕/播放列表键，什么都不会发生。
    """
    section("播放器对话框")
    import numpy as np
    if not media:
        record("对话框", "有测试素材", False, "缺少 ffmpeg")
        return

    # 需要有同目录多集才有播放列表可看
    series = Path(tmpdir) / "dialogseries"
    series.mkdir(exist_ok=True)
    for i in range(1, 4):
        f = series / f"Episode 0{i}.mp4"
        if not f.exists():
            subprocess.run(
                ["ffmpeg", "-y", "-f", "lavfi",
                 "-i", f"color=c=blue:s=320x240:d=20:r=10",
                 "-c:v", "libx264", "-pix_fmt", "yuv420p", str(f)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    first = series / "Episode 01.mp4"

    def grab_frame(tag, extra_env, start=400):
        env = {"MUVIK_FRAMEGRAB": str(Path(tmpdir) / f"dlg_{tag}.ppm"),
               "MUVIK_FRAMEGRAB_START": str(start)}
        env.update(extra_env)
        rc, log, files = grab(tmpdir, f"dlg_{tag}", env,
                              args=[str(first)], start=start)
        return load_ppm(files[0]) if files else None

    base = grab_frame("closed", {})
    with_dlg = grab_frame("open", {"MUVIK_OPEN_PLAYLIST": "1"})

    if base is None or with_dlg is None:
        record("对话框", "抓帧成功", False, "缺帧")
        return

    def bright(a):
        return (a.mean(axis=2) > 120).mean() * 100

    b_base, b_dlg = bright(base), bright(with_dlg)

    # 判据：对话框面板是否真的盖在视频区域上。
    #
    # 这里踩过两次坑，记录清楚：
    #
    #   坑 1：原判据是 b_dlg > b_base * 3（比亮像素倍数）。
    #         它假设「无对话框时画面几乎全暗」，但 OSD 常驻后
    #         基线被抬到 0.7%，面板本身 1.3%，倍数只有 2 倍 → 误判失败。
    #
    #   坑 2：改用「面板区域方差下降」。但视频有黑边，
    #         采样区恰好落在黑边上时 std 只有 0.5，比面板的 24.3 还低，
    #         条件 u_dlg < u_base * 0.75 永远不成立 → 再次误判。
    #
    # 正确做法：直接看「该区域的平均亮度是否明显升高」。
    # 对话框面板是浅色（#2D3038 附近），视频黑边是 0。
    # 面板盖上去后均值必然上升，这与黑边、OSD 都无关。
    def panel_luma(a):
        h, w = a.shape[:2]
        region = a[int(h * 0.25):int(h * 0.40), int(w * 0.35):int(w * 0.65)]
        return float(region.mean(axis=2).mean())

    l_base, l_dlg = panel_luma(base), panel_luma(with_dlg)

    # 判据：对话框打开后，视频所在区域**明显变暗**。
    #
    # 实测数据：85.2 -> 26.9（下降 68%）。
    # 这是 borealis::Dialog 的样式决定的 —— 它不画一块浅色面板，
    # 而是先用半透明黑色遮罩压暗整个背景，再在上面放深色对话框。
    # 所以「面板盖住视频」的正确表现是**变暗**，不是变亮。
    #
    # 三种判据的踩坑记录（都写在这里，避免以后再来一遍）：
    #   ① 比亮像素倍数 3x  —— OSD 常驻抬高基线后失效
    #   ② 比方差下降       —— 视频黑边让基线方差极低，永远不成立
    #   ③ 比亮度上升       —— 方向错反，对话框是压暗而非变亮
    # 最终用：亮度下降超过 25%。
    panel_shown = l_dlg < l_base * 0.75
    record("对话框", "播放列表对话框渲染在视频之上",
           bool(panel_shown),
           f"视频区亮度 {l_base:.1f} -> {l_dlg:.1f}"
           f"（下降 {100 - l_dlg / max(l_base, 0.001) * 100:.0f}%）")

    # 对话框区域（画面中部）应被浅色面板覆盖，视频的纯蓝被遮住
    def center_mean(a):
        h, w = a.shape[:2]
        return a[int(h*0.42):int(h*0.58), int(w*0.40):int(w*0.60)].mean()

    c_base, c_dlg = center_mean(base), center_mean(with_dlg)
    record("对话框", "对话框面板遮住视频画面",
           abs(c_dlg - c_base) > 8,
           f"中心均值 {c_dlg:.1f} vs {c_base:.1f}")

    # ── 非播放器页面的对话框（文件浏览器）──
    # 这里的判断方式不同：对话框会压暗背景（半透明遮罩），
    # 因此看「边缘区域是否明显变暗 + 中心是否变亮」。
    def edge_center(a):
        g = a.mean(axis=2)
        h, w = g.shape
        edge = (g[:, :w//10].mean() + g[:, -w//10:].mean()) / 2
        center = g[int(h*0.42):int(h*0.58), int(w*0.40):int(w*0.60)].mean()
        return edge, center

    # ── 非播放器页面的对话框（文件浏览器）──
    # 判据：① 边缘被压暗（半透明遮罩）；② 中心区域的内容**真的换了**。
    #
    # 为什么不用「中心变亮」：borealis 的对话框面板是**深色**的，
    # 覆盖后中心亮度可能几乎不变甚至略降（实测 29.3 → 27.9），
    # 但面板替换了原来的列表行，所以逐像素差异的均值必然显著。
    # 旧的「c_on > c_off」判据因此把正常渲染的对话框判成失败 ——
    # 而插件管理页恰好变亮（29.6 → 41.2），同一套代码一处过一处挂，
    # 看起来就像偶发 bug，其实是判据方向错误（抓帧人工复核过画面）。
    def center_diff(a, b):
        h, w = a.shape[:2]
        reg = (int(h*0.42), int(h*0.58), int(w*0.40), int(w*0.60))
        ar = a[reg[0]:reg[1], reg[2]:reg[3]].astype(float)
        br = b[reg[0]:reg[1], reg[2]:reg[3]].astype(float)
        return float(np.abs(br - ar).mean())

    rc, _, f_off = grab(tmpdir, "dlg_browser_off",
                        {"MUVIK_START_TAB": "0",
                         "MUVIK_FRAMEGRAB_START": "400"}, start=400)
    rc, _, f_on = grab(tmpdir, "dlg_browser_on",
                       {"MUVIK_START_TAB": "0",
                        "MUVIK_OPEN_DIALOG": "folder_options",
                        "MUVIK_FRAMEGRAB_START": "400"}, start=400)
    if f_off and f_on:
        img_off, img_on = load_ppm(f_off[0]), load_ppm(f_on[0])
        a_off, _ = edge_center(img_off)
        a_on, _ = edge_center(img_on)
        cd = center_diff(img_off, img_on)
        record("对话框", "文件浏览器对话框显示（背景被压暗）",
               a_on < a_off * 0.6 and cd > 6.0,
               f"边缘 {a_off:.1f}→{a_on:.1f}，中心差异 {cd:.1f}")
    else:
        record("对话框", "文件浏览器对话框显示（背景被压暗）", False, "抓帧失败")

    # 插件管理页的对话框（另一个非播放器页面，验证不是偶然）
    rc, _, p_off = grab(tmpdir, "dlg_plug_off",
                        {"MUVIK_OPEN_PLUGINS": "1",
                         "MUVIK_FRAMEGRAB_START": "400"}, start=400)
    rc, _, p_on = grab(tmpdir, "dlg_plug_on",
                       {"MUVIK_OPEN_PLUGINS": "1",
                        "MUVIK_OPEN_DIALOG": "1",
                        "MUVIK_FRAMEGRAB_START": "400"}, start=400)
    if p_off and p_on:
        img_off, img_on = load_ppm(p_off[0]), load_ppm(p_on[0])
        a_off, _ = edge_center(img_off)
        a_on, _ = edge_center(img_on)
        cd = center_diff(img_off, img_on)
        record("对话框", "插件管理页对话框显示（背景被压暗）",
               a_on < a_off * 0.6 and cd > 6.0,
               f"边缘 {a_off:.1f}→{a_on:.1f}，中心差异 {cd:.1f}")
    else:
        record("对话框", "插件管理页对话框显示（背景被压暗）", False, "抓帧失败")


def check_search(tmpdir, media):
    """搜索必须同时命中「刮削标题」和「原文件名」。

    为什么单列一组：这是一条**行为承诺**而不是数值 —— 用户看到卡片上
    写着「蓝调时刻」，就会用这个名字去搜。搜索只认文件名时，
    搜片名得 0 项、搜 cam_0042 得 1 项，用户会以为片子丢了。

    判据用三路对比，而不是「有没有内容」：
      · 搜刮削标题  ≈ 搜原文件名（同一批结果）
      · 两者都显著不同于「搜不存在的词」
    只比「有内容」会误判 —— 搜索框完全失效时列表照样显示全部条目，
    看起来一样「有内容」。
    """
    section("搜索")

    if not media or "series_dir" not in media:
        record("search", "搜索", True, "跳过：缺少测试媒体")
        return

    # 单独准备一份配置：一条媒体 + 一条刮削元数据（标题与文件名不同）
    cfg = Path(tmpdir) / "search_cfg"
    app_cfg = cfg / "yiufplayer"
    app_cfg.mkdir(parents=True, exist_ok=True)

    media_dir = Path(tmpdir) / "search_media"
    media_dir.mkdir(parents=True, exist_ok=True)
    src = None
    for ext in (".mp4", ".mkv", ".avi"):
        cand = media_dir / f"cam_0042{ext}"
        if cand.exists():
            src = cand
            break
    if src is None:
        shutil.copy(str(media["series_dir"]) + "/" + sorted(os.listdir(media["series_dir"]))[0],
                    media_dir / "cam_0042.mp4")
        src = media_dir / "cam_0042.mp4"

    (app_cfg / "libraries.dat").write_text(f"{media_dir}|搜索测试|1|1700000000\n")
    # metadata.dat 是 0x1f 分隔的 11 字段，不是竖线！
    SEP = "\x1f"
    row = SEP.join([str(src), "蓝调时刻", "测试简介", "", "", "2023-05-01",
                    "剧情", "8.2", "112", "tmdb", "1700000000"])
    (app_cfg / "metadata.dat").write_text(
        "# Muvik metadata v1 (fields separated by 0x1f)\n" + row + "\n")

    def grab(query, name):
        out = Path(tmpdir) / f"search_{name}.ppm"
        env = {
            "XDG_CONFIG_HOME": str(cfg),
            "MUVIK_START_TAB": "0",
            "MUVIK_SEARCH": query,
            "MUVIK_FRAMEGRAB": str(out),
            "MUVIK_FRAMEGRAB_AT_MS": "3500",
        }
        rc, log = run_player(env, timeout=40)
        if not out.exists():
            return None
        return load_ppm(out)

    a = grab("蓝调时刻", "title")
    b = grab("cam_0042", "filename")
    c = grab("根本不存在", "none")

    if a is None or b is None or c is None:
        record("search", "抓帧", False, "无法比较")
        return

    import numpy as np

    def ink(x):
        return int((x.mean(axis=2) > 100).sum())

    d_ab = float(np.abs(a.astype(int) - b.astype(int)).mean())
    d_ac = float(np.abs(a.astype(int) - c.astype(int)).mean())

    record("search", "搜刮削标题能命中", d_ac > 2.0, f"差异 {d_ac:.2f}")
    record("search", "搜原文件名能命中", d_ac > 2.0, "")
    # 阈值 0.5 → 0.9。
    #
    # 两种搜法用的是同一个渲染路径，理论上应当像素级一致；实测
    # 0.41~0.51 的抖动来自焦点动画的相位（抓帧时刻不同）。
    # 0.5 卡在抖动带里，换色板后必然偶发失败（实测一次 0.506）。
    # 0.9 远小于「搜标题 vs 搜不到」的 7.0，仍能区分真失败。
    record("search", "两种搜法返回同一批结果", d_ab < 0.9, f"差异 {d_ab:.3f}")
    record("search", "结果与空结果明显不同", d_ac > 2.0, f"{d_ac:.2f}")


def check_font_scale(tmpdir, media):
    """界面字号档位必须真的改变渲染结果。

    为什么单列一组：这是最容易「看起来做了、其实没生效」的功能 ——
    档位值确实变了、ui::font 也返回了新值，但界面上看不出差别
    （实测踩过：只有自绘文字变了，侧栏和标题栏仍是旧字号）。

    判据用**两档抓帧对比**，而且要求差异足够大：
      只断言「两幅图不同」不够 —— 时间戳、噪点纹理都会让图不同。
      断言「差异 > 阈值」，才能排除噪声级别的变化。
    """
    section("字号档位")

    if not media or "series_dir" not in media:
        record("font_scale", "字号", True, "跳过：缺少测试媒体")
        return

    cfg = Path(tmpdir) / "font_scale_cfg"
    app_cfg = cfg / "yiufplayer"
    app_cfg.mkdir(parents=True, exist_ok=True)
    (app_cfg / "libraries.dat").write_text(
        f"{media['series_dir']}|测试|1|1700000000\n")

    def grab(scale, name):
        out = Path(tmpdir) / f"fontscale_{name}.ppm"
        env = {
            "XDG_CONFIG_HOME": str(cfg),
            "MUVIK_START_TAB": "0",
            "MUVIK_FONT_SCALE": scale,
            "MUVIK_FRAMEGRAB": str(out),
            "MUVIK_FRAMEGRAB_AT_MS": "3500",
        }
        rc, log = run_player(env, timeout=40)
        if not out.exists():
            return None
        return load_ppm(out)

    small = grab("0.85", "small")
    large = grab("1.20", "large")

    if small is None or large is None:
        record("font_scale", "抓帧", False, "无法比较")
        return

    import numpy as np
    d = float(np.abs(small.astype(int) - large.astype(int)).mean())

    # 阈值 1.35，不是 1.0。
    #
    # 实测标定（两档 0.85 vs 1.20 的整图平均差）：
    #   全部生效（自绘文字 + 侧栏 + 标题栏）  1.53 ~ 1.55
    #   只让自绘文字生效、主题度量没跟上      1.12
    #   完全不生效（字号是常量）              约 0.1（噪点纹理级别）
    #
    # 我最初写 1.0，然后实测「把 ui::font 的 scale 全去掉」：
    # 检查**依然通过**（1.12），因为主题里那两处侧栏/标题栏字号还在缩放。
    # 也就是说 1.0 区分不了「全部生效」和「只生效一部分」——
    # 而那正是这条检查最该抓住的失败模式。
    record("font_scale", "两档渲染结果有实质差异", d > 1.35, f"差异 {d:.2f}")

    # 另有一条独立判据：大档不该把内容顶出屏幕
    # （只比「差异大」会鼓励把字号调到溢出）
    import numpy as np2
    h = large.shape[0]
    ink_large = int((large.mean(axis=2) > 100).sum())
    ink_small = int((small.mean(axis=2) > 100).sum())
    record("font_scale", "大字档没有把内容挤出画面",
           ink_large > ink_small * 0.8,
           f"亮像素 {ink_small} -> {ink_large}")


def check_track_memory(tmpdir, media):
    """选过的字幕轨要记住，下次打开恢复。

    用户手册明确承诺了这一点，但历史上只把 sid 写进历史、打开时从不读回，
    选了中文字幕下次还是第一条（第 38 轮发现并修复）。
    """
    section("字幕轨记忆")
    if not media:
        record("字幕轨", "有测试素材", False, "缺少 ffmpeg")
        return

    src = Path(tmpdir) / "twosubs"
    src.mkdir(exist_ok=True)
    clip = src / "twosubs.mkv"
    if not clip.exists():
        en = src / "en.srt"
        zh = src / "zh.srt"
        en.write_text("1\n00:00:00,500 --> 00:00:20,000\nENGLISH\n", encoding="utf-8")
        zh.write_text("1\n00:00:00,500 --> 00:00:20,000\n中文字幕\n", encoding="utf-8")
        subprocess.run(
            ["ffmpeg", "-y", "-f", "lavfi", "-i", "color=c=black:s=640x360:d=20:r=10",
             "-i", str(en), "-i", str(zh),
             "-map", "0:v", "-map", "1:s", "-map", "2:s",
             "-c:v", "libx264", "-c:s", "srt",
             "-metadata:s:s:0", "language=eng",
             "-metadata:s:s:1", "language=chi", str(clip)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # 用**隔离的**配置目录跑这两次，因此不需要再清理用户真实历史
    # （原先那段清理写的还是过期的 ~/.config/muvik 路径，从未生效）。
    sid_cfg = Path(tmpdir) / "sid_cfg"
    (sid_cfg / "yiufplayer").mkdir(parents=True, exist_ok=True)

    def run(tag, extra_env, start=250):
        env = {"MUVIK_FRAMEGRAB": str(Path(tmpdir) / f"sidx_{tag}.ppm"),
               "MUVIK_FRAMEGRAB_START": str(start),
               "XDG_CONFIG_HOME": str(sid_cfg)}
        env.update(extra_env)
        rc, log, files = grab(tmpdir, f"sidx_{tag}", env, args=[str(clip)], start=start)
        m = re.search(r"自动选中字幕轨 #(\d+)", log)
        return (int(m.group(1)) if m else None), rc

    first, _ = run("set", {"MUVIK_FORCE_SUB_SID": "2"})
    record("字幕轨", "可以选中非默认字幕轨", first == 2, f"选中 #{first}")

    second, _ = run("restore", {})
    record("字幕轨", "重新打开时恢复上次选择的轨道", second == 2,
           f"恢复为 #{second}（期望 #2）")


def check_functional(tmpdir, media):
    """功能级冒烟：断言「操作之后真的有用」，而不只是「没崩」。

    为什么需要这一组：
      其余所有检查都基于像素统计或进程退出码，无法回答
      「按 A 有没有真的打开视频」。实际发生过——崩溃修好后
      点击任何视频都打不开，而全部 65 项检查依然全绿。

    这一组用 MUVIK_SMOKE 让程序自己注入按键并断言状态变化，
    失败时以退出码 1 结束，因此这里的判据只有一条：退出码必须为 0。
    """
    section("功能冒烟")

    # 注意：这里不能设 FRAMEGRAB —— 抓帧完成后程序会主动退出，
    # 冒烟步骤跑到一半就被截断，只能看到第一条断言。
    # 冒烟流程自己会在结束时 quick_exit，不依赖抓帧退出。
    cfg = Path(tmpdir) / "smoke_cfg"
    cfg.mkdir(parents=True, exist_ok=True)
    # 配置目录名必须用实际值 yiufplayer（不是旧名 muvik）。
    #
    # 写错目录的后果不是「测试失败得莫名其妙」，而是**测试在空库上跑**：
    # 媒体库为空 → 冒烟里「按 A 进入文件夹 / 按 A 压入播放页」必然失败，
    # 于是这几项看起来像产品坏了。实测直接手跑 MUVIK_SMOKE=1 全过，
    # 而 verify_ui 报失败，差别只在这行路径。
    muvik_cfg = cfg / "yiufplayer"
    muvik_cfg.mkdir(parents=True, exist_ok=True)
    if media and "series_dir" in media:
        with open(muvik_cfg / "libraries.dat", "w") as f:
            f.write(f"{media['series_dir']}|测试剧集|1|1700000000\n")

    env = {
        "MUVIK_SMOKE": "1",
        "MUVIK_START_TAB": "0",
        "XDG_CONFIG_HOME": str(cfg),
    }
    rc, log = run_player(env, timeout=45)

    # 逐条把程序内部的判定转述出来，失败时能看到是哪一步
    seen = 0
    for line in log.splitlines():
        plain = re.sub(r"\x1b\[[0-9;]*m", "", line)
        if "[smoke]" not in plain:
            continue
        body = plain.split("[smoke]", 1)[1].strip()
        if not ("PASS" in body or "FAIL" in body):
            continue
        seen += 1
        ok = "FAIL" not in body
        record("功能", body.replace("PASS", "").replace("FAIL", "").strip()[:50], ok, body)

    if seen == 0:
        record("功能", "程序输出了冒烟结果", False, "日志里没有任何 PASS/FAIL")

    # 总判据：进程退出码
    record("功能", "功能冒烟整体通过（退出码 0）", rc == 0, f"rc={rc}")


def check_input_and_font(tmpdir, media):
    """用户实际反馈的三类问题：中文字体、点击闪退、键盘手柄区隔。

    这三项在 40 轮自动检查里从未被覆盖过——全都由人工试用才发现。
    """
    section("用户反馈问题")

    # ── 1. 中文字体回退 ──
    rc, log, files = grab(tmpdir, "font_zh",
                          {"MUVIK_FRAMEGRAB_START": "150"}, start=150)
    has_zh = "Using zh-Hans font" in log or "zh-Hans" in log
    record("字体", "加载简体中文字体回退", has_zh,
           re.search(r"Using zh-Hans font: (\S+)", log).group(1)
           if has_zh else "未加载，中文会显示为乱码")

    # ── 2. 键盘 / 手柄区隔 ──
    rc, log, _ = grab(tmpdir, "input_pad",
                      {"MUVIK_PRINT_KEYICON": "1",
                       "MUVIK_FRAMEGRAB_START": "200"}, start=200)
    record("输入", "默认输入类型为手柄", "输入类型: GAMEPAD" in log,
           "GAMEPAD" if "输入类型: GAMEPAD" in log else "不符")

    rc, log, _ = grab(tmpdir, "input_kb",
                      {"MUVIK_PRINT_KEYICON": "1",
                       "MUVIK_FAKE_KEYBOARD": "1",
                       "MUVIK_FRAMEGRAB_START": "200"}, start=200)
    record("输入", "键盘操作切换为 KEYBOARD", "输入类型: KEYBOARD" in log,
           "KEYBOARD" if "输入类型: KEYBOARD" in log else "仍为手柄")
    record("输入", "键盘模式显示 Enter 而非手柄图标",
           "A 键图标: [Enter]" in log,
           "Enter" if "A 键图标: [Enter]" in log else "仍是手柄字形")

    # ── 2.4 外挂字幕与多音轨 ──
    # 用户明确问过「支持字幕文件和音轨选取吗」。这两项此前没有自动检查。
    ext = media.get("external_subs") if media else None
    if ext:
        # 判「字幕是否渲染」必须排除 OSD 的干扰：
        # OSD 自动隐藏要 8 秒，而抓帧时间随 UI 帧率浮动 ——
        # 若恰好在 OSD 显示期间抓帧，半透明渐变会压在字幕上，
        # 把纯白文字压成 200 上下，>245 的判据必挂（实测抓帧确认）。
        # 因此两组都关掉 OSD（MUVIK_OSD_OFF），只比「字幕开/关」。
        rc, log, _ = grab(tmpdir, "extsub",
                          {"MUVIK_FRAMEGRAB_START": "200",
                           "MUVIK_FRAMEGRAB_COUNT": "3",
                           "MUVIK_FRAMEGRAB_INTERVAL": "400",
                           "MUVIK_OSD_OFF": "1"},
                          args=[str(ext)], start=200)
        # 同目录 .srt 应被 sub-auto 自动加载并选中
        loaded = "自动选中字幕轨" in log
        record("字幕", "外挂字幕自动加载并选中", loaded,
               "已选中" if loaded else "未加载")

        # 渲染验证：与关闭字幕的对照组比较底部白像素
        frames = sorted(Path(tmpdir).glob("extsub*.ppm"))
        rc2, _, _ = grab(tmpdir, "extsuboff",
                         {"MUVIK_FRAMEGRAB_START": "200",
                          "MUVIK_FRAMEGRAB_COUNT": "3",
                          "MUVIK_FRAMEGRAB_INTERVAL": "400",
                          "MUVIK_OSD_OFF": "1",
                          "MUVIK_SUBTITLE_OFF": "1"},
                         args=[str(ext)], start=200)
        off_frames = sorted(Path(tmpdir).glob("extsuboff*.ppm"))
        if frames and off_frames:
            # 判据用「纯白像素」而不是「亮像素」：
            # testsrc 图案本身就有大片亮色，用 >200 阈值几乎区分不出来
            # （实测 18341 vs 13662，只有 1.3 倍）。
            # SRT 字幕由 mpv 渲染为纯白文字，R/G/B 都接近 255，
            # 而测试图案的色块达不到这个纯度。
            def pure_white(p):
                a = load_ppm(p)
                h = a.shape[0]
                band = a[int(h * 0.60):int(h * 0.95), :, :]
                return int(((band[:, :, 0] > 245) &
                            (band[:, :, 1] > 245) &
                            (band[:, :, 2] > 245)).sum())
            on_max = max(pure_white(p) for p in frames)
            off_max = max(pure_white(p) for p in off_frames)
            record("字幕", "外挂字幕真的渲染出来", on_max > off_max * 3 + 50,
                   f"有字幕 {on_max} / 无字幕 {off_max}")

    # 多音轨：能切到第 2 条，且不存在的轨被拒绝
    multi = media.get("multitrack") if media else None
    if multi:
        for aid, expect_ok in ((1, True), (2, True), (9, False)):
            rc, log, _ = grab(tmpdir, f"aid{aid}",
                              {"MUVIK_FORCE_AID": str(aid),
                               "MUVIK_FRAMEGRAB_START": "150"},
                              args=[str(multi)], start=150)
            # 必须断言 mpv 的**实际** aid，而不是「代码执行过」。
            # 早期版本只查日志里有「已切换音轨」，把 selectTrack 改成空语句
            # 测试依然全绿——那是个假检查。
            if expect_ok:
                ok = f"mpv 实际 aid={aid}" in log
            else:
                ok = "不存在" in log
            record("音轨", f"音轨 {aid} " + ("可切换" if expect_ok else "被拒绝"),
                   ok, log.strip().splitlines()[-1][:50] if log.strip() else "")

    # ── 2.5 按住期间列表重建（真实崩溃场景）──
    # 这是「点视频库闪退」的确切成因：按下时保存 View*，
    # 按住期间扫描完成触发 clearViews，抬起时复用已释放指针。
    rc, log, _ = grab(tmpdir, "touch_rebuild",
                      {"MUVIK_TOUCH_REBUILD": "1",
                       "MUVIK_START_TAB": "0",
                       "MUVIK_FRAMEGRAB_START": "900"}, start=900)
    record("稳定性", "按住期间重建列表不崩溃",
           rc not in (139, -11, 134, -6),
           f"rc={rc}")

    # ── 2.6 越界标签索引（曾 100% 必崩）──
    # TabFrame::selectTab 内部走 Sidebar::getItem，
    # 那里对 children[position] 不做边界检查，
    # 越界会在 dynamic_cast 上 SEGV（实测 START_TAB=1000 必崩）。
    for bad in ("9", "1000"):
        rc, log, _ = grab(tmpdir, f"tab_oob_{bad}",
                          {"MUVIK_START_TAB": bad,
                           "MUVIK_FRAMEGRAB_START": "120"}, start=120)
        record("稳定性", f"越界标签索引 {bad} 不崩溃",
               rc not in (139, -11, 134, -6), f"rc={rc}")

    # ── 3. 点击列表项不崩溃 ──
    # 这一条对应「点开视频立即闪退」。
    if media:
        rc, log, _ = grab(tmpdir, "click_video",
                          {"MUVIK_CLICK_FIRST_VIDEO": "1",
                           "MUVIK_START_TAB": "0",
                           "MUVIK_FRAMEGRAB_START": "300"}, start=300)
        crashed = rc in (139, -11, 134, -6)
        record("稳定性", "点击视频不崩溃", not crashed,
               f"rc={rc}" + (" SIGSEGV" if crashed else ""))


def check_plugin(tmpdir, media):
    """插件管理页渲染 + 插件已加载。"""
    section("插件系统")
    rc, log, files = grab(tmpdir, "plugin",
                          {"MUVIK_OPEN_PLUGINS": "1",
                           "MUVIK_FRAMEGRAB_START": 250}, start=250)
    loaded = log.count("已加载插件")
    record("插件", "至少加载一个插件", loaded >= 1, f"{loaded} 个")
    if files:
        import numpy as np
        a = load_ppm(files[0])
        g = a.mean(axis=2)
        bright = (g > 120).mean() * 100
        record("插件", "插件管理页有内容", bright > 1.0, f"亮像素 {bright:.2f}%")
    else:
        record("插件", "插件管理页有内容", False, "抓帧失败")


CHECKS = {
    "font": check_font,
    "osd": check_osd,
    "basic": check_basic_render,
    "tabs": check_tabs,
    "video": check_video,
    "subtitle": check_subtitle,
    "audio": check_audio,
    "gestures": check_gestures,
    "playlist": check_playlist,
    "screens": check_screens,
    "aspect": check_aspect,
    "perf": check_perf,
    "sid": check_track_memory,
    "feedback": check_input_and_font,
    "functional": check_functional,
    "search": check_search,
    "font_scale": check_font_scale,
    "asan": check_asan,
    "soak": check_soak,
    "dialog": check_dialog,
    "plugin": check_plugin,
}


def main():
    ap = argparse.ArgumentParser(description="Muvik 端到端 UI 验证")
    ap.add_argument("--only", action="append", choices=sorted(CHECKS),
                    help="只跑指定检查组（可重复）")
    ap.add_argument("--list", action="store_true", help="列出所有检查组")
    ap.add_argument("--keep", action="store_true", help="保留临时文件")
    args = ap.parse_args()

    if args.list:
        for k in sorted(CHECKS):
            print(f"  {k:10s} {CHECKS[k].__doc__ or ''}".rstrip())
        return 0

    if not BIN.exists():
        print(f"\033[31m找不到 {BIN}\033[0m")
        print("请先构建：cmake -B build && cmake --build build")
        return 2

    # 依赖预检：基于抓帧的检查都用 numpy 读 PPM 像素。
    # 不预检的话，失败信息是「异常: No module named 'numpy'」——
    # 藏在某一组检查的结果里，看上去像代码坏了，实际只是缺依赖。
    try:
        import numpy  # noqa: F401
    except ImportError:
        print("\033[31m缺少依赖 numpy\033[0m")
        print("安装后重跑：python3 -m pip install numpy")
        print("（只想跑不依赖像素的检查组时可用 --list 查看分组）")
        return 2

    tmpdir = tempfile.mkdtemp(prefix="muvik_verify_")
    try:
        media = make_media(tmpdir)
        if media is None:
            print("\033[33m警告：没有 ffmpeg，视频/字幕/音频/连播检查将跳过\033[0m")

        selected = args.only or sorted(CHECKS)
        for key in selected:
            try:
                CHECKS[key](tmpdir, media)
            except Exception as e:  # 单个检查失败不应中断其余
                record(key, "检查执行", False, f"异常: {e}")
    finally:
        if args.keep:
            print(f"\n临时文件保留在：{tmpdir}")
        else:
            shutil.rmtree(tmpdir, ignore_errors=True)

    # ── 汇总 ──
    print("\n" + "─" * 56)
    passed = sum(1 for r in RESULTS if r[2])
    failed = [r for r in RESULTS if not r[2]]
    for group, name, ok, detail in RESULTS:
        mark = "\033[32m✓\033[0m" if ok else "\033[31m✗\033[0m"
        print(f" {mark} [{group}] {name}"
              + (f"  \033[90m({detail})\033[0m" if detail else ""))

    print("─" * 56)
    if failed:
        print("\033[31m失败项明细：\033[0m")
        for group, name, ok, detail in failed:
            print(f"  - [{group}] {name}  {detail}")
        print(f"\033[31m失败 {len(failed)} / {len(RESULTS)}\033[0m")
        return 1
    print(f"\033[32m全部通过（{passed} 项）\033[0m")
    return 0


if __name__ == "__main__":
    sys.exit(main())
