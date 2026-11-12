#!/bin/bash
# 把源码增量推送到 Steam Deck 并在上面编译运行。
#
# 为什么这样做：
#   Steam Deck 本身就是 Linux x86_64 机器，不需要交叉编译。
#   在 Deck 上编译能保证 100% 对齐目标环境（Arch、AMD Mesa、
#   真实硬件解码），这是容器和 WSL2 都做不到的。
#
#   传输只同步源码（32MB 级），不传构建产物，rsync 增量后通常 <1 秒；
#   Deck 上增量编译（配合 ccache）通常 10-30 秒。
#
# 用法：
#   DECK_HOST=deck@192.168.31.49 ./scripts/deploy-deck.sh
#   DECK_HOST=deck@steamdeck.local ./scripts/deploy-deck.sh --run   # 编译后直接启动
#
# 前置（在 Deck 上执行一次）：
#   sudo steamos-readonly disable
#   sudo pacman -S --needed base-devel cmake ninja pkgconf git rsync \
#        mpv ffmpeg glfw mesa libcurl fmt tinyxml2 dbus
#   sudo steamos-readonly enable
#   # 或者在 distrobox 里做（推荐，系统更新不会重置）：
#   distrobox create -n deckdev -i archlinux:latest
#   distrobox enter deckdev

set -euo pipefail

DECK_HOST="${DECK_HOST:-deck@steamdeck.local}"
REMOTE_DIR="${REMOTE_DIR:-~/muvik}"
RUN_AFTER=0
[ "${1:-}" = "--run" ] && RUN_AFTER=1

echo "=== 目标: $DECK_HOST:$REMOTE_DIR ==="

if ! ssh -o ConnectTimeout=5 -o BatchMode=yes "$DECK_HOST" true 2>/dev/null; then
    echo "无法连接 $DECK_HOST" >&2
    echo "检查：" >&2
    echo "  1. Deck 与电脑在同一网络" >&2
    echo "  2. Deck 上已开启 SSH：" >&2
    echo "     sudo systemctl enable --now sshd" >&2
    echo "  3. 已配置免密登录：" >&2
    echo "     ssh-copy-id $DECK_HOST" >&2
    exit 1
fi

echo "=== 同步源码（排除构建产物）==="
rsync -az --delete \
    --exclude 'build*/' \
    --exclude '.git/' \
    --exclude 'android/app/build/' \
    --exclude 'android/.gradle/' \
    --exclude '*.o' \
    --exclude '*.ppm' \
    ./ "$DECK_HOST:$REMOTE_DIR/"

echo "=== 远程编译（增量）==="
# -j 用 Deck 的一半核心，避免编译时界面卡死
ssh "$DECK_HOST" "cd $REMOTE_DIR && \
    cmake -B build-linux -DCMAKE_BUILD_TYPE=Release -DPLATFORM_DESKTOP=ON -DUSE_SYSTEM_GLFW=OFF >/dev/null && \
    cmake --build build-linux -j\$(nproc | awk '{print int(\$1/2)+1}')"

echo "=== 完成 ==="

if [ "$RUN_AFTER" = "1" ]; then
    echo "=== 启动（按 Ctrl+C 停止）==="
    ssh -t "$DECK_HOST" "cd $REMOTE_DIR && ./build-linux/muvik"
else
    echo "运行：ssh -t $DECK_HOST 'cd $REMOTE_DIR && ./build-linux/muvik'"
fi

echo
echo "首次在 Deck 上编译前请确认已装依赖（见脚本头部注释）。"
echo "SteamOS 根分区只读，装包需先 steamos-readonly disable；"
echo "系统更新会重置这些包，因此长期开发建议用 distrobox。"
