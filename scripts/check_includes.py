#!/usr/bin/env python3
"""检查头文件是否缺少它直接用到的标准库 include。

为什么需要：libc++（macOS）会间接包含很多标准头，
libstdc++（Linux/SteamOS）不会。同一个文件在 macOS 上编得过、
在 Linux 上却报 'std::vector does not name a template type'。

这个脚本扫描 include/ 与 src/ 下的所有文件，检查常见标准类型的
使用与 include 是否匹配。
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 类型/符号 → 需要的头文件
SYMBOLS = {
    "std::vector": "<vector>",
    "std::string": "<string>",
    "std::unordered_map": "<unordered_map>",
    "std::map": "<map>",
    "std::set": "<set>",
    "std::unique_ptr": "<memory>",
    "std::shared_ptr": "<memory>",
    "std::make_unique": "<memory>",
    "std::make_shared": "<memory>",
    "std::function": "<functional>",
    "std::optional": "<optional>",
    "std::mutex": "<mutex>",
    "std::thread": "<thread>",
    "std::atomic": "<atomic>",
    "std::chrono": "<chrono>",
    "std::ifstream": "<fstream>",
    "std::ofstream": "<fstream>",
    "std::ostringstream": "<sstream>",
    "std::stringstream": "<sstream>",
    "std::sort": "<algorithm>",
    "std::find_if": "<algorithm>",
    "std::clamp": "<algorithm>",
    "std::max": "<algorithm>",
    "std::min": "<algorithm>",
    "std::getenv": "<cstdlib>",
    "std::atoi": "<cstdlib>",
    "std::time": "<ctime>",
    "std::fprintf": "<cstdio>",
    "std::printf": "<cstdio>",
}


def check_file(path: Path) -> list[str]:
    try:
        text = path.read_text(errors="ignore")
    except OSError:
        return []
    # 去掉注释，避免注释里的示例代码误报
    code = re.sub(r"//[^\n]*", "", text)
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)

    missing = []
    for sym, header in SYMBOLS.items():
        if sym not in code:
            continue
        # 头文件里已经 include 了就不算缺失
        if header in text:
            continue
        missing.append(f"{sym} → {header}")
    return missing


def main() -> int:
    targets = []
    for sub in ("include", "src"):
        targets.extend(sorted((ROOT / sub).rglob("*.hpp")))
        targets.extend(sorted((ROOT / sub).rglob("*.cpp")))

    failed = 0
    for path in targets:
        # 只检查项目自己的代码，跳过 external
        if "external" in path.parts:
            continue
        missing = check_file(path)
        if missing:
            rel = path.relative_to(ROOT)
            print(f"  ✗ {rel}")
            for m in missing:
                print(f"      {m}")
            failed += 1

    if failed:
        print(f"\n{failed} 个文件缺少标准库 include（Linux 上会编译失败）")
        return 1
    print(f"检查通过（{len(targets)} 个文件）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
