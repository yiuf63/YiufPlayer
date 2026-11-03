#!/bin/bash
# 生成 resources/romfs/ —— libromfs 的资源镜像。
#
# 为什么需要它：
#   Android/iOS 没有稳定的相对资源路径，资源必须编译进二进制。
#   borealis 的 libromfs 会把这个目录下的文件全部嵌入，
#   运行时通过 romfs::get("font/font.ttf") 访问。
#
# 为什么不提交到仓库：
#   它是 resources/ 的子集副本，字体一份就 10MB，重复提交没有意义。
#   构建前跑一次即可。
#
# 用法：
#   ./scripts/prepare-romfs.sh

set -e
cd "$(dirname "$0")/.."

SRC=resources
DST=resources/romfs

echo "=== 生成 $DST ==="
rm -rf "$DST"
mkdir -p "$DST"

# 只复制运行时真正需要的子目录。
# 不带 gamepad/：手柄数据库由 Android 侧单独从 assets 读取。
for d in font i18n icon img material xml; do
    if [ -d "$SRC/$d" ]; then
        cp -r "$SRC/$d" "$DST/"
        echo "  $d"
    fi
done

SIZE=$(du -sh "$DST" | cut -f1)
COUNT=$(find "$DST" -type f | wc -l | tr -d ' ')
echo "=== 完成：$COUNT 个文件，$SIZE ==="
