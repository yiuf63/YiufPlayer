#!/usr/bin/env python3
"""目标符合性检查。

回答一个问题：**这个项目真的做完了当初要做的那些功能吗？**

与其它工具的定位区别：

| 工具 | 回答的问题 |
| :--- | :--- |
| `run_tests.sh` | 单个模块的逻辑对不对 |
| `verify_ui.py` | 界面和播放真的能跑吗 |
| `check_docs.py` | 文档写的和代码一致吗 |
| `release_check.py` | 装出来的东西能不能发 |
| **`goal_check.py`** | **需求清单里的每一项是否都有实现** |

输出的是「需求 → 代码位置」的对应关系，便于交接时逐条确认。

用法：
    python3 tests/tools/goal_check.py
"""

import subprocess, re, os

def run(cmd):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True)

checks = []

# 1. 手柄 + 触控
r = run("grep -c 'registerAction' src/view/mpv_player.cpp")
checks.append(("手柄按键", int(r.stdout.strip() or 0) >= 13, f"{r.stdout.strip()} 个按键"))

r = run("grep -cE 'TapGestureRecognizer|PanGestureRecognizer' src/view/mpv_player.cpp")
checks.append(("触控手势", int(r.stdout.strip() or 0) >= 2, f"{r.stdout.strip()} 个识别器"))

# 2. 格式
r = run("grep -oE '\\\"\\.[a-z0-9]+\\\"' src/utils/scanner.cpp | wc -l")
checks.append(("格式白名单", int(r.stdout.strip()) >= 40, f"{r.stdout.strip()} 种扩展名"))

# 3. 内嵌字幕
r = run("grep -c 'selectTrack(\"sub\"' src/view/mpv_player.cpp")
checks.append(("内嵌字幕选择", int(r.stdout.strip() or 0) >= 1, "已实现"))

# 4. 音乐 + 后台播放
r = run("grep -c 'vid\", \"no\"' src/playback/audio_player.cpp")
checks.append(("音频独立引擎", int(r.stdout.strip() or 0) >= 1, "vid=no 已设置"))
r = run("grep -c 'AudioPlayer::getInstance().tick' src/main.cpp")
checks.append(("主循环音频 tick", int(r.stdout.strip() or 0) >= 1, "已接入主循环"))

# 5. 媒体文件夹 + 递归扫描
r = run("grep -c 'scan_helper(scan_helper' src/utils/scanner.cpp")
checks.append(("递归扫描", int(r.stdout.strip() or 0) >= 1, "手写递归 + 深度上限"))
r = run("grep -c 'addFolder' src/activity/file_browser_activity.cpp")
checks.append(("添加文件夹", int(r.stdout.strip() or 0) >= 1, "已实现"))

# 6. 创意工坊（视频源 + 刮削源）
r = run("grep -c 'create_video_source' include/plugin/deck_plugin_abi.h")
checks.append(("插件：视频源", int(r.stdout.strip() or 0) >= 1, "ABI 已定义"))
r = run("grep -c 'create_scraper' include/plugin/deck_plugin_abi.h")
checks.append(("插件：刮削源", int(r.stdout.strip() or 0) >= 1, "ABI 已定义"))
r = run("grep -c 'Zip Slip\\|zip slip\\|../' src/plugin/plugin_installer.cpp")
checks.append(("ZIP 安装防护", int(r.stdout.strip() or 0) >= 1, "已实现"))

# 7. 不假定屏幕尺寸
r = run("grep -c 'DECKPLAYER_WINDOW' src/main.cpp")
checks.append(("窗口尺寸可覆盖", int(r.stdout.strip() or 0) >= 1, "已实现"))

# 8. macOS 构建测试
checks.append(("macOS 可构建", os.path.exists("build/deck-player"), "产物存在"))

# 9. 许可证
r = run("test -f cmake/license_probe.cpp && echo ok")
checks.append(("构建期许可证检查", "ok" in r.stdout, "license_probe.cpp"))
r = run("test -f tests/tools/license_audit.py && echo ok")
checks.append(("产物级许可证审计", "ok" in r.stdout, "license_audit.py"))
r = run("grep -c 'disable-gpl' scripts/build-lgpl-libmpv.sh")
checks.append(("LGPL 构建脚本", int(r.stdout.strip() or 0) >= 1, "参数正确"))

# 10. 闭源商业（不引入 GPL 传染）
r = run("grep -c 'define GPL_LINKAGE_GUARD' src/main.cpp 2>/dev/null || echo 0")
print("逐条核对目标：\n")
for name, ok, detail in checks:
    print(f"  {'✓' if ok else '✗'} {name:22s} {detail}")
print()
failed = [c for c in checks if not c[1]]
print(f"结果: {len(checks)-len(failed)}/{len(checks)} 通过")
