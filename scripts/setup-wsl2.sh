#!/bin/bash
# 在 Windows 的 WSL2 里准备 DeckPlayer 的构建环境。
#
# 为什么用 WSL2：
#   它是真实 Linux 内核 + 原生 x86_64，和 Steam Deck 同架构。
#   对比：macOS 上跑 amd64 容器是 QEMU 模拟，GL 起不来（实测
#   "glfw: failed to initialize"），只能验证编译，不能验证运行。
#
# 用法（在 WSL2 的 Ubuntu 终端里）：
#   bash scripts/setup-wsl2.sh
#
# 前置：
#   Windows 上先装 WSL2 + Ubuntu：
#     wsl --install -d Ubuntu
#   然后从 /mnt/c/... 或 git clone 拿到本项目源码。

set -e

echo "=== 环境检查 ==="
ARCH=$(uname -m)
echo "  架构: $ARCH"
if [ "$ARCH" != "x86_64" ]; then
    echo "警告：当前不是 x86_64（$ARCH）。Steam Deck 是 x86_64，"
    echo "      在非 x86_64 上构建的产物不能直接用于 Deck。"
fi

if ! grep -qi microsoft /proc/version 2>/dev/null; then
    echo "提示：看起来不是 WSL 环境，脚本仍可继续（普通 Linux 也适用）"
fi

echo "=== 安装构建依赖 ==="
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git rsync \
    libmpv-dev \
    libavutil-dev libavformat-dev libavcodec-dev \
    libcurl4-openssl-dev \
    libgl1-mesa-dev libglu1-mesa-dev \
    libglfw3-dev \
    libfmt-dev \
    libtinyxml2-dev \
    libdbus-1-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    mesa-utils

echo "=== 检查 GL（WSLg）==="
if command -v glxinfo >/dev/null 2>&1; then
    glxinfo -B 2>/dev/null | grep -E "OpenGL renderer|OpenGL version" | head -2 || \
        echo "  glxinfo 无输出（WSLg 可能未就绪，GUI 仍可试）"
else
    echo "  无 glxinfo，跳过"
fi

echo "=== 检查 GLFW 是否为 borealis 定制分支 ==="
GLFW_DIR="external/borealis/library/lib/extern/glfw"
if [ -f "$GLFW_DIR/include/GLFW/glfw3.h" ] && \
   grep -q "GLFW_SOFT_FULLSCREEN" "$GLFW_DIR/include/GLFW/glfw3.h"; then
    echo "  ✅ 定制分支就绪"
else
    echo "  ❌ 缺少 GLFW 子模块（必须用 xfangfang/glfw 定制分支）"
    echo "     系统 libglfw3-dev 编译会报："
    echo "       error: 'GLFW_SOFT_FULLSCREEN' was not declared"
    echo "     解决："
    echo "       cd external/borealis && git submodule update --init library/lib/extern/glfw"
    exit 1
fi

echo
echo "=== 完成 ==="
echo "构建：./scripts/build-linux.sh"
echo "运行：./build-linux/deck-player"
echo
echo "注意：WSLg 的 GL 走 D3D12 翻译（Mesa d3d12 驱动），"
echo "      与 Steam Deck 的 AMD Mesa 不同。它能验证"
echo "      「界面能不能出、交互对不对」，但不能代表 Deck 上的"
echo "      真实性能与硬件解码行为。最终仍需 Deck 实机验证。"
