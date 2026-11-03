#!/bin/bash
# 编译 libromfs-generator（宿主机可执行文件）。
#
# 为什么必须现编：
#   它把 resources/romfs/ 打成 C++ 源码嵌进二进制，
#   是**在宿主机上运行**的工具，不是目标平台代码。
#   因此不能入库（macOS 编出来是 Mach-O，Linux runner 跑不了；
#   Linux 编出来是 ELF，macOS 又跑不了）。
#
#   这个坑实际踩过：把 macOS 的生成器提交进仓库后，
#   Linux CI 直接报
#     "libromfs-generator has not been built"
#   因为它是 Mach-O，在 Ubuntu 上既跑不了、CMake 也不认为它可用。
#
# 用法：
#   bash scripts/build-libromfs-generator.sh
#
# 产物：external/borealis/libromfs-generator

set -e
cd "$(dirname "$0")/.."

TARGET="external/borealis/libromfs-generator"
SRC="external/borealis/library/lib/extern/libromfs/generator"

if [ ! -d "$SRC" ]; then
    echo "找不到 libromfs 生成器源码: $SRC" >&2
    exit 1
fi

# 已存在且是**本机可执行格式**才跳过。
# 只判 -x 不够：从 macOS 拷来的 Mach-O 在 Linux 上也有 x 位，
# 但一执行就报 "cannot execute binary file"。
if [ -x "$TARGET" ]; then
    if "$TARGET" >/dev/null 2>&1; then
        echo "  生成器已存在且可执行，跳过"
        exit 0
    fi
    # 能跑起来返回非 0 也算可用（无参数时它打印用法并退出 1）
    if "$TARGET" 2>&1 | grep -qi "usage\|libromfs\|<"; then
        echo "  生成器已存在且可执行，跳过"
        exit 0
    fi
    echo "  已存在的生成器无法在本机执行，重新编译"
fi

echo "=== 编译 libromfs-generator ==="
rm -f "$TARGET"

BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# 独立配置：只编 generator，不牵扯主项目与交叉编译工具链
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null

# 生成器可能落在不同位置，找到可执行文件后拷到 borealis 期望的路径
FOUND=$(find "$BUILD" -maxdepth 3 -type f -perm -u+x -name "libromfs-generator*" ! -name "*.cmake" | head -1)
if [ -z "$FOUND" ]; then
    echo "编译完成但找不到产物" >&2
    find "$BUILD" -maxdepth 2 -type f | head -10 >&2
    exit 1
fi

cp "$FOUND" "$TARGET"
chmod +x "$TARGET"
echo "=== 完成：$TARGET ==="
