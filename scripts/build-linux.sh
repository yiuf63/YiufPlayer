#!/bin/bash
# 在 Linux 上构建 Muvik（用于 Steam Deck / SteamOS 验证）
#
# 用法（推荐在容器里跑，见根目录 Dockerfile.linux）：
#   docker build --platform linux/amd64 -f Dockerfile.linux -t muvik-linux .
#   docker run --rm -v "$PWD:/src" muvik-linux
#
# 直接在 Linux 主机上跑也可以，需要先装好构建依赖（见 Dockerfile.linux）。

set -e

BUILD_DIR="${BUILD_DIR:-build-linux}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
# 是否从视频抽帧做缩略图（需要链接 FFmpeg 的 libav*）。
#
# 默认 ON（开发用）。给 SteamOS/Arch 做发布包时建议 THUMBNAILS=OFF：
# 打开时产物会把 Debian 的 libavcodec.so.59 / libavutil.so.57 写进依赖，
# 而 SteamOS 是 Arch、FFmpeg soname 不同，拷过去直接报找不到共享库。
# 关掉后只依赖 libmpv（SteamOS 自带、soname 稳定），代价是网格少了缩略图。
#
#   THUMBNAILS=OFF ./scripts/build-linux.sh
THUMBNAILS="${THUMBNAILS:-ON}"

echo "=== 检查依赖 ==="
missing=()
for pkg in mpv libcurl gl fmt tinyxml2; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        missing+=("$pkg")
    fi
done
# FFmpeg 只在开缩略图时才需要
if [ "$THUMBNAILS" = "ON" ]; then
    for pkg in libavutil libavformat libavcodec libswscale; do
        if ! pkg-config --exists "$pkg" 2>/dev/null; then
            missing+=("$pkg")
        fi
    done
fi
if [ ${#missing[@]} -gt 0 ]; then
    echo "缺少 pkg-config 依赖: ${missing[*]}" >&2
    echo "Debian/Ubuntu: 见 Dockerfile.linux 的 apt-get 列表" >&2
    exit 1
fi
echo "  依赖齐全（缩略图: $THUMBNAILS）"

echo "=== 检查 GLFW 子模块 ==="
# **必须用 submodule 里的 glfw，不能用系统的。**
#
# borealis 用的是 xfangfang/glfw 定制分支（见 external/borealis/.gitmodules），
# 它增加了 GLFW_SOFT_FULLSCREEN / GLFW_TOUCH / GLFW_IME /
# glfwSetTouchCallback / glfwSetIMEStatusCallback 等扩展。
# 官方 libglfw3-dev 没有这些符号，编译会直接报：
#   error: 'GLFW_SOFT_FULLSCREEN' was not declared in this scope
GLFW_DIR="external/borealis/library/lib/extern/glfw"
if [ ! -f "$GLFW_DIR/CMakeLists.txt" ]; then
    echo "缺少 GLFW 子模块：$GLFW_DIR" >&2
    echo "请执行：" >&2
    echo "  cd external/borealis && git submodule update --init library/lib/extern/glfw" >&2
    echo "（或手动 clone https://github.com/xfangfang/glfw.git 到该目录）" >&2
    exit 1
fi
if ! grep -q "GLFW_SOFT_FULLSCREEN" "$GLFW_DIR/include/GLFW/glfw3.h" 2>/dev/null; then
    echo "GLFW 子模块不是 borealis 定制分支（缺少 GLFW_SOFT_FULLSCREEN）" >&2
    echo "请改用 https://github.com/xfangfang/glfw.git" >&2
    exit 1
fi
echo "  GLFW 子模块就绪（含定制 API）"

echo "=== 配置 ==="
THUMB_FLAG="ON"
[ "$THUMBNAILS" = "OFF" ] && THUMB_FLAG="OFF"
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DPLATFORM_DESKTOP=ON \
    -DUSE_SYSTEM_GLFW=OFF \
    -DUSE_SYSTEM_SDL2=OFF \
    -DMUVIK_ENABLE_THUMBNAILS="$THUMB_FLAG" \
    "$@"

echo "=== 编译 ==="
JOBS=$(nproc 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" -j"$JOBS"

echo
echo "=== 完成 ==="
ls -lh "$BUILD_DIR/muvik" 2>/dev/null || echo "  未找到产物"
echo
echo "运行前确认 SteamOS 上装有："
echo "  mpv-libs 或 libmpv, ffmpeg, glfw, libcurl, fmt, tinyxml2"
