#!/usr/bin/env bash
# ============================================================================
# 更新 Flatpak manifest 中的源码哈希
#
# flatpak manifest 要求每个源码归档提供 sha256。手写容易出错且难以核对，
# 本脚本从网络下载对应版本并计算真实哈希，直接回填到 manifest。
#
# 用法：
#   ./scripts/update-flatpak-sources.sh
#
# 需要网络。运行后请用 `git diff`（若有版本控制）核对改动。
# ============================================================================

set -euo pipefail
cd "$(dirname "$0")/.." || exit 1

MANIFEST="flatpak/org.muvik.Muvik.yaml"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

compute_sha256() {
    local url="$1"
    local name="$2"
    echo ">>> 下载 $name" >&2
    if ! curl -fL --retry 3 --progress-bar -o "$TMPDIR/$name" "$url"; then
        echo "下载失败: $url" >&2
        return 1
    fi
    shasum -a 256 "$TMPDIR/$name" | awk '{print $1}'
}

update_field() {
    local placeholder="$1"
    local real="$2"
    local label="$3"
    if grep -q "$placeholder" "$MANIFEST"; then
        # 用 | 作分隔符，避免哈希中的 / 冲突
        sed -i.bak "s|$placeholder|$real|" "$MANIFEST"
        rm -f "$MANIFEST.bak"
        echo "    ✓ $label -> $real"
    else
        echo "    - $label 已是真实值，跳过"
    fi
}

echo "=== 更新 Flatpak 源码哈希 ==="

FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz"
MPV_URL="https://github.com/mpv-player/mpv/archive/refs/tags/v0.39.0.tar.gz"

FFMPEG_SHA="$(compute_sha256 "$FFMPEG_URL" "ffmpeg-7.1.tar.xz")"
MPV_SHA="$(compute_sha256 "$MPV_URL" "mpv-0.39.0.tar.gz")"

echo
echo ">>> 回填 manifest"
update_field "REPLACE_WITH_REAL_SHA256_OF_FFMPEG_7.1" "$FFMPEG_SHA" "FFmpeg 7.1"
update_field "REPLACE_WITH_REAL_SHA256_OF_MPV_0.39.0" "$MPV_SHA" "mpv 0.39.0"

echo
echo "=== 完成。请检查 $MANIFEST ==="
