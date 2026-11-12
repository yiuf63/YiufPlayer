#!/usr/bin/env bash
# ============================================================================
# 构建 LGPL 版 libmpv（含 LGPL 版 FFmpeg）
#
# 用途：为 Muvik 的闭源发布形态提供一个许可干净的 libmpv。
#       GPL 版 libmpv/FFmpeg 会使链接产物受 GPL 约束，不能闭源分发。
#
# 产出：$PREFIX 下的一套动态库，CMake 通过 -DMPV_ROOT=$PREFIX 使用。
#
# 用法：
#   ./scripts/build-lgpl-libmpv.sh [安装前缀]
#   PREFIX=/opt/muvik-lgpl ./scripts/build-lgpl-libmpv.sh
#
# 依赖：meson ninja pkg-config C 编译器 nasm(可选，提升 x86 性能)
# ============================================================================

set -euo pipefail

PREFIX="${1:-${PREFIX:-$HOME/.local/muvik-lgpl}}"
WORK="${WORK:-$(pwd)/.build-lgpl}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

FFMPEG_VERSION="${FFMPEG_VERSION:-7.1}"
MPV_VERSION="${MPV_VERSION:-0.39.0}"

echo "============================================================"
echo " 构建 LGPL 版 libmpv"
echo "------------------------------------------------------------"
echo " 安装前缀 : $PREFIX"
echo " 工作目录 : $WORK"
echo " 并发数   : $JOBS"
echo " FFmpeg   : $FFMPEG_VERSION"
echo " mpv      : $MPV_VERSION"
echo "============================================================"

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "错误：缺少依赖 '$1'" >&2; exit 1; }
}
need meson
need ninja
need pkg-config

mkdir -p "$WORK" "$PREFIX"
cd "$WORK"

# ---------------------------------------------------------------------------
# 隔离 pkg-config，防止「静默链接到系统 GPL 版 FFmpeg」
#
# 陷阱：pkg-config 即使不设 PKG_CONFIG_PATH，也会搜索默认路径
# （/usr/lib/pkgconfig、/opt/homebrew/lib/pkgconfig 等）。如果 mpv 在这时
# 找到的是 Homebrew 的 GPL FFmpeg，构建会「成功」，但产物仍然是 GPL——
# 而且没有任何报错。这是本项目最危险的失败模式。
#
# 对策：把 pkg-config 的默认搜索路径清空，只允许显式指定的 $PREFIX。
# 这样一旦 LGPL FFmpeg 没装好，构建会【立刻失败】而不是静默降级。
# ---------------------------------------------------------------------------
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig"
# 禁止 pkg-config 回退到系统路径
unset PKG_CONFIG_SYSROOT_DIR 2>/dev/null || true

echo ">>> pkg-config 搜索路径已锁定到：$PKG_CONFIG_LIBDIR"

# ---------------------------------------------------------------------------
# 1. FFmpeg —— 关键：绝不带 --enable-gpl / --enable-version3 / --enable-nonfree
# ---------------------------------------------------------------------------
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
    echo ">>> 下载 FFmpeg ${FFMPEG_VERSION}"
    curl -fL --retry 3 -o "ffmpeg-${FFMPEG_VERSION}.tar.xz" \
        "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
    tar xf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
fi

pushd "ffmpeg-${FFMPEG_VERSION}" >/dev/null

echo ">>> 配置 FFmpeg（LGPL 模式）"
# 说明：
#   --disable-gpl        禁用 GPL-only 组件（必需）
#   --disable-version3   若未加此参数，部分组件会升级到 GPLv3
#   --disable-nonfree    禁用不可再分发组件（必需）
#   --enable-shared      动态链接，满足 LGPL 可替换要求
./configure \
    --prefix="$PREFIX" \
    --enable-shared \
    --disable-static \
    --disable-gpl \
    --disable-version3 \
    --disable-nonfree \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    $( [ "$(uname -s)" = "Darwin" ] && echo "--enable-videotoolbox --enable-audiotoolbox" )

echo ">>> 编译 FFmpeg（约需数分钟）"
make -j"$JOBS"
make install
popd >/dev/null

# 立刻用官方 API 验证产物许可证，避免白忙一场
echo ">>> 验证 FFmpeg 许可证"
cat > /tmp/_liccheck.c <<'EOF'
#include <libavutil/avutil.h>
#include <stdio.h>
int main(void) {
    printf("%s\n", avutil_license());
    return 0;
}
EOF
LIC=$(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig" \
      cc /tmp/_liccheck.c -o /tmp/_liccheck \
      $(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig" pkg-config --cflags --libs libavutil) \
      && /tmp/_liccheck)
echo "    avutil_license() = $LIC"
case "$LIC" in
    *GPL*)
        echo "错误：构建出的 FFmpeg 仍是 GPL，发布链路不成立。" >&2
        exit 1
        ;;
esac
echo "    ✓ 非 GPL，可用于闭源分发"

# ---------------------------------------------------------------------------
# 2. mpv —— 同样禁用 GPL
# ---------------------------------------------------------------------------
if [ ! -d "mpv-${MPV_VERSION}" ]; then
    echo ">>> 下载 mpv ${MPV_VERSION}"
    curl -fL --retry 3 -o "mpv-${MPV_VERSION}.tar.gz" \
        "https://github.com/mpv-player/mpv/archive/refs/tags/v${MPV_VERSION}.tar.gz"
    tar xf "mpv-${MPV_VERSION}.tar.gz"
fi

pushd "mpv-${MPV_VERSION}" >/dev/null

echo ">>> 配置 mpv（LGPL 模式，仅 libmpv）"
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}" \
meson setup build \
    --prefix="$PREFIX" \
    --buildtype=release \
    -Dgpl=false \
    -Dlibmpv=true \
    -Dcplayer=false \
    -Dbuild-date=false

echo ">>> 编译 mpv"
ninja -C build -j"$JOBS"
ninja -C build install
popd >/dev/null

# ---------------------------------------------------------------------------
# 3. 产物级复核：直接对生成的 libmpv 做依赖链审计
#
# 前面只验证了「我们编出来的 FFmpeg 是 LGPL」。但 mpv 最终链接到哪个
# libavutil，取决于 meson 的解析结果。这里顺着 libmpv 的动态库依赖再查一遍，
# 确保没有悄悄链回系统版本。
# ---------------------------------------------------------------------------
echo ">>> 产物级复核：libmpv 的依赖链"
LIBMPV=$(find "$PREFIX" -name "libmpv*.dylib" -o -name "libmpv.so*" 2>/dev/null | head -1)
if [ -z "$LIBMPV" ]; then
    echo "错误：没找到构建出的 libmpv，安装可能失败。" >&2
    exit 1
fi
echo "    libmpv: $LIBMPV"

if [ "$(uname -s)" = "Darwin" ]; then
    DEPS=$(otool -L "$LIBMPV" | grep -oE "/[^ ]*libavutil[^ ]*dylib" | head -1)
else
    DEPS=$(ldd "$LIBMPV" | grep -oE "/[^ ]*libavutil[^ ]*so[^ ]*" | head -1)
fi

if [ -z "$DEPS" ]; then
    echo "错误：libmpv 没有链接到 libavutil，构建不完整。" >&2
    exit 1
fi
echo "    libavutil: $DEPS"

# 关键：它必须指向 $PREFIX 内部，而不是 /opt/homebrew 或 /usr/lib
case "$DEPS" in
    "$PREFIX"/*)
        echo "    ✓ 指向本次构建的 $PREFIX"
        ;;
    *)
        echo "错误：libmpv 链接的 libavutil 不在 $PREFIX 内！" >&2
        echo "      实际: $DEPS" >&2
        echo "      这通常是 pkg-config 回退到系统路径导致的。" >&2
        echo "      该产物很可能受 GPL 约束，不能用于闭源发布。" >&2
        exit 1
        ;;
esac

# 用官方 API 再问一次这个库的许可证
cat > /tmp/_liccheck2.c <<'EOF'
#include <libavutil/avutil.h>
#include <stdio.h>
int main(void) { printf("%s\n", avutil_license()); return 0; }
EOF
LIC2=$(cc /tmp/_liccheck2.c -o /tmp/_liccheck2 \
       -I"$PREFIX/include" \
       -L"$PREFIX/lib" -Wl,-rpath,"$PREFIX/lib" -lavutil 2>/dev/null \
       && /tmp/_liccheck2 2>/dev/null || echo "?")
echo "    avutil_license() = $LIC2"
case "$LIC2" in
    *GPL*)
        echo "错误：最终产物的 FFmpeg 仍是 GPL，不能用于闭源分发。" >&2
        exit 1
        ;;
esac

echo
echo "============================================================"
echo " 完成。LGPL 版 libmpv 已安装到：$PREFIX"
echo "------------------------------------------------------------"
echo " 在 Muvik 中使用："
echo "   cmake -B build -DMPV_ROOT=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX"
echo "   cmake --build build"
echo "============================================================"
