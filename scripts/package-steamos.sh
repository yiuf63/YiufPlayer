#!/bin/bash
# 打一个可直接拷到 Steam Deck / SteamOS 上运行的发布包。
#
# 为什么需要单独打包：
#   容器里编译出来的二进制不能只拷自己 —— 它还要 resources/（字体、图标、
#   i18n、gamepad db），否则界面上一个中文字都显示不出来（见 AGENTS.md 陷阱 1）。
#   另外 SteamOS 上没有 cmake，不能像 deploy-deck.sh 那样在设备上现编译。
#
# 用法：
#   ./scripts/package-steamos.sh                 # 默认 linux/amd64
#   ./scripts/package-steamos.sh x86_64          # 显式指定
#
# 产物：dist/yiufplayer-steamos-<arch>-<日期>.tar.gz
#
# 两种构建模式，默认选 thin：
#   thin  (THUMBNAILS=OFF) —— 产物不直接依赖 libav*，只依赖 libmpv。
#                             这是**给 SteamOS 用的默认值**：Debian 容器里的
#                             libavcodec.so.59 与 Arch 上 FFmpeg 的 soname 不同，
#                             直接链接会在 Deck 上启动失败。
#                             代价：媒体库网格没有视频缩略图，显示占位画面。
#   full  (THUMBNAILS=ON)  —— 有缩略图，但要求目标机的 FFmpeg soname 与
#                             构建环境一致。在 SteamOS 上**大概率跑不起来**，
#                             只在同发行版（如 Ubuntu 桌面）分发时用。

set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:-x86_64}"
MODE="${MODE:-thin}"
DATE="$(date +%Y%m%d)"
STAGE="dist/yiufplayer-steamos-${ARCH}-${DATE}"
TARBALL="${STAGE}.tar.gz"

if [ "$MODE" = "full" ]; then
    THUMBNAILS=ON
    BUILD_DIR=build-linux
else
    THUMBNAILS=OFF
    BUILD_DIR=build-steamos
fi

echo "=== 1/4 容器内构建（模式: ${MODE}，缩略图: ${THUMBNAILS}）==="
if ! docker image inspect muvik-linux >/dev/null 2>&1; then
    echo "镜像不存在，先构建（首次约 10 分钟）"
    docker build --platform "linux/amd64" -f Dockerfile.linux -t muvik-linux .
fi
docker run --rm --platform linux/amd64 \
    -v "$PWD:/src" -w /src \
    -e "THUMBNAILS=$THUMBNAILS" -e "BUILD_DIR=$BUILD_DIR" \
    muvik-linux bash scripts/build-linux.sh

if [ ! -f "$BUILD_DIR/yiufplayer" ]; then
    echo "构建失败：找不到 $BUILD_DIR/yiufplayer" >&2
    exit 1
fi

echo "=== 2/4 检查产物依赖 ==="
# 关键校验：thin 模式下产物不应**直接**依赖 libav*。
# 注意区分「直接依赖」（objdump -p 的 NEEDED）与「传递依赖」（ldd 展开后
# 由 libmpv 带进来的）。只有直接依赖才会因 SteamOS 的 soname 不同而失败。
#
# 这段必须在**容器里**跑：macOS 自带的 objdump 是 LLVM 版本、不认 ELF，
# 在宿主上执行会得到空结果，从而把「有依赖」误判成「干净」。
DEP_INFO="$(docker run --rm --platform linux/amd64 \
    -v "$PWD:/src" -w /src muvik-linux \
    bash -c "objdump -p '$BUILD_DIR/yiufplayer' | grep NEEDED")"
printf '%s\n' "$DEP_INFO" | sed 's/^/    /'
DIRECT_AV="$(printf '%s\n' "$DEP_INFO" | grep -c 'libav' || true)"
if [ "$MODE" = "thin" ] && [ "$DIRECT_AV" != "0" ]; then
    echo "  ⚠ thin 模式下仍直接依赖 $DIRECT_AV 个 libav* 库" >&2
    echo "  （预期为 0；SteamOS 上可能因 soname 不匹配而启动失败）" >&2
    exit 1
fi
echo "  直接依赖的 libav* 数量: $DIRECT_AV  $([ "$DIRECT_AV" = "0" ] && echo '（SteamOS 安全）')"

echo "=== 3/4 组装发布目录 ==="
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD_DIR/yiufplayer" "$STAGE/"
# 兼容别名：老脚本/文档里还写着 muvik
ln -sf yiufplayer "$STAGE/muvik"
# resources 必须一起带上，否则字体与图标全部缺失
cp -R resources "$STAGE/"
# 去掉不需要进发布包的东西（romfs 是编译期用的中间产物）
rm -rf "$STAGE/resources/romfs"

cat > "$STAGE/run.sh" <<'LAUNCH'
#!/bin/sh
# 启动脚本：把资源路径固定到本目录，避免受调用者工作目录影响。
#
# 为什么需要：borealis 把资源路径编译成了相对路径 "./resources/"，
# 程序靠 ResourceLocator 在启动时找资源目录并切换工作目录。
# 从 Steam 快捷方式启动时工作目录不可控，这里显式 cd 过去最稳。
cd "$(dirname "$0")" || exit 1
export MUVIK_RESOURCES="$PWD/resources"
exec ./yiufplayer "$@"
LAUNCH
chmod +x "$STAGE/run.sh"

cat > "$STAGE/README.txt" <<'READMEEOF'
Yiuf Player — SteamOS / Linux 发布包
=====================================

## 首次运行前（二选一）

### A. 直接跑（想先看看能不能起来）
    ./run.sh /path/to/video.mp4

如果报 "error while loading shared libraries"，说明缺运行库，
见下面「系统依赖」。

### B. 装成正式版（推荐）
    # 1. 拷到用户目录
    mkdir -p ~/yiufplayer
    cp -R ./* ~/yiufplayer/

    # 2. 注册到 Steam（在 Desktop Mode 下执行）
    cd ~/yiufplayer
    python3 add_to_steam.py

    # 3. 回到 Gaming Mode，在「非 Steam 游戏」里能找到 Yiuf Player

## 系统依赖

SteamOS 自带 libmpv、libcurl、mesa，通常无需安装。
如果缺库，先解锁只读分区：

    sudo steamos-readonly disable
    sudo pacman -Sy --needed mpv ffmpeg mesa libcurl fmt tinyxml2 dbus
    sudo steamos-readonly enable

注意：SteamOS 的系统更新会重置这些包，升级后可能需要重装。
长期使用建议放在 distrobox 里。

## 本包的一个取舍

打这个包时**关闭了「从视频抽帧生成缩略图」**（链接 FFmpeg 的那部分）。
原因是：构建容器是 Debian，它链接 libavcodec.so.59 / libavutil.so.57；
SteamOS 是 Arch，FFmpeg soname 不同，带上这些依赖会在 Deck 上一启动就报错。

代价：媒体库网格里的影片卡片显示的是**自动生成的占位画面**，
而不是真实视频截图。其余功能（播放、字幕、音轨、插件、后台音乐）不受影响。

## 校验这个包

    # 应该只看到 libmpv / libcurl / libdbus / libc 等，没有 libav*
    objdump -p yiufplayer | grep NEEDED

## 已知限制（诚实说明）

- **未在 Steam Deck 实机验证过**。本包是在 macOS 上用 Debian 容器交叉产出的，
  编译通过、符号完整，但手柄映射、触屏、硬件解码（VAAPI）、休眠唤醒
  都还没在真机上跑过。
- 若启动后黑屏或无响应，请把终端输出贴出来；`run.sh` 已把资源路径显式设好。
READMEEOF

cp scripts/add_to_steam.py "$STAGE/" 2>/dev/null || true

echo "=== 4/4 打包 ==="
tar -czf "$TARBALL" -C dist "$(basename "$STAGE")"
ls -lh "$TARBALL"
echo
echo "完成。拷到 Steam Deck 后："
echo "  scp $TARBALL deck@steamdeck:~"
echo "  ssh deck@steamdeck 'tar xzf $(basename "$TARBALL") && cd $(basename "$STAGE") && ./run.sh'"
