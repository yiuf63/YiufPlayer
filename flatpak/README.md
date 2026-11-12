# Flatpak 构建说明

## 前置条件

```bash
# 安装构建工具与运行时
flatpak install flathub org.freedesktop.Sdk//24.08 \
                       org.freedesktop.Platform//24.08 \
                       org.freedesktop.Sdk.Extension.llvm18//24.08
```

## 构建前：填入真实源码哈希

manifest 里的 `sha256` 是**占位符**，不是真实值。请先执行：

```bash
./scripts/update-flatpak-sources.sh
```

脚本会下载对应版本源码并计算真实哈希回填。

> 为什么不直接写死哈希？
> 这些数值无法在离线环境核实，手填容易出错且难以发现。
> 与其留一个看起来像真值的错误哈希（会导致 `flatpak-builder` 报
> 一个费解的校验失败），不如明确标为占位符。

## 构建

```bash
flatpak-builder --user --install --force-clean \
    build-flatpak flatpak/org.muvik.Muvik.yaml
```

## 运行

```bash
flatpak run org.muvik.Muvik
```

在 Steam Deck 的 Gaming Mode 中，通过 Steam 的「添加非 Steam 游戏」添加
`org.muvik.Muvik`，即可用手柄直接启动。

## 许可证合规（重要）

本 manifest 产出**不含 GPL-only 组件**的构建：

| 组件 | 关键选项 | 作用 |
| :--- | :--- | :--- |
| FFmpeg | `--disable-gpl` | 禁用 GPL-only 组件 |
| FFmpeg | `--disable-version3` | 防止部分组件升级到 GPLv3 |
| FFmpeg | `--disable-nonfree` | 禁用不可再分发组件 |
| mpv | `-Dgpl=false` | 不编译 GPL-only 源文件 |

本项目已改为 **GPL-3.0 分发**，所以即使上面某项被移除、产物受 GPL 约束，
**也不再与许可证冲突**（此前闭源时才是致命问题）。

保留这些选项的理由是让产物依赖更轻、分发义务更简单：
LGPL 配置的 FFmpeg 只需提供 FFmpeg 自身的源码，不需要按 GPL §6 处理整个依赖链。

Muvik 构建时会调用 `avutil_license()` 打印实际许可证（见
`cmake/license_probe.cpp`）。它现在**只记录、不中止构建**；
manifest 里不再需要 `-DALLOW_GPL_LINKAGE=OFF`（该开关已随闭源约束一并移除）。

### 构建后复核

```bash
# 容器内检查 FFmpeg 许可证字符串
flatpak run --command=sh org.muvik.Muvik -c \
  'strings /app/lib/libavutil.so.* | grep -m1 "GPL" || echo "未发现 GPL 标记"'
```

## 已知限制

- **未在真实 Flatpak 环境验证过**：本 manifest 的字段与选项经过静态校验
  （YAML 合法、关键 GPL 选项存在），但沙箱环境无 flatpak 工具与网络，
  未执行过完整构建。
- `--filesystem=host` 已移除，改为按需挂载 `xdg-videos` / `xdg-music` /
  `/run/media` 等。这减少了沙箱暴露面，但用户若把媒体放在非常规路径，
  需要自行用 Flatseal 等工具授权。
- 首次启动若字体缺失，检查资源目录是否正确安装到
  `/app/share/muvik/resources`。程序会在启动日志打印
  `资源目录: <path>`，可据此排查。
