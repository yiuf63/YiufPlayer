# Linux / SteamOS 构建

目标是在 **Steam Deck（SteamOS，x86_64 + Arch）** 上运行。
开发机是 macOS，因此用容器产出 Linux 二进制。

## 方式一：容器构建（推荐）

```bash
# 1. 构建镜像（只需一次，约 10 分钟，取决于网速）
docker build --platform linux/amd64 -f Dockerfile.linux -t muvik-linux .

# 2. 编译（源码挂载进去，产物落在 build-linux/）
docker run --rm -v "$PWD:/src" -w /src muvik-linux
```

产物：`build-linux/muvik`

## 方式二：在 Linux 主机上直接构建

先装依赖（Debian/Ubuntu）：

```bash
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  libmpv-dev libavutil-dev libavformat-dev libavcodec-dev \
  libcurl4-openssl-dev libgl1-mesa-dev libglu1-mesa-dev \
  libglfw3-dev libfmt-dev libtinyxml2-dev libdbus-1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

然后：

```bash
./scripts/build-linux.sh
```

## SteamOS 上的运行时依赖

SteamOS 是只读根文件系统，需要先解锁再装包：

```bash
sudo steamos-readonly disable
sudo pacman -Sy --needed mpv ffmpeg glfw mesa libcurl fmt tinyxml2
sudo steamos-readonly enable
```

> ⚠️ SteamOS 的系统更新会重置 pacman 安装的包，升级后需要重装。

## 打发布包（给 Steam Deck 用）

```bash
./scripts/package-steamos.sh
# 产物：dist/yiufplayer-steamos-x86_64-<日期>.tar.gz
```

包里带 `yiufplayer` + `resources/` + `run.sh` + `add_to_steam.py` + 说明。
拷到 Deck 解压后 `./run.sh` 即可。

### 关键取舍：默认关闭视频缩略图

**这是让产物能在 SteamOS 上跑起来的关键。**

构建容器是 Debian bookworm，它链接的是 `libavcodec.so.59 / libavutil.so.57`
（FFmpeg 5.1）。SteamOS 是 Arch，FFmpeg 版本更高、**soname 不同**。
如果产物直接依赖这些库，拷到 Deck 上会直接报：

```
error while loading shared libraries: libavcodec.so.59
```

所以 `package-steamos.sh` 默认用 `THUMBNAILS=OFF` 构建，产物的 `NEEDED`
表里就没有 `libav*`（只剩 libmpv / libcurl / libdbus / libc 等），
跨发行版稳定。代价是媒体库网格显示占位画面而不是视频截图。

需要缩略图时（例如只在你自己的 Ubuntu 桌面上分发）：

```bash
MODE=full ./scripts/package-steamos.sh    # 保留 FFmpeg 链接
```

> 判断标准：`objdump -p yiufplayer | grep NEEDED` 里**直接列出**的
> `libav*` 才致命。`ldd` 展开后看到的那些是由 `libmpv.so` 自己带进来的
> 传递依赖，不受影响 —— 这两个概念别混。

## 为什么不用交叉编译

macOS → Linux 交叉编译需要目标平台的 `libmpv`、`libGL`、X11 等预编译库。
这些在容器里天然就有，所以用容器比搭交叉工具链省事得多。

## 已知注意事项

### 1. GLFW 必须用子模块里的定制分支

borealis 用的是 `xfangfang/glfw` 定制分支，多了
`GLFW_SOFT_FULLSCREEN` / `GLFW_TOUCH` / `GLFW_IME` / `glfwSetTouchCallback`
等扩展。系统 `libglfw3-dev` 没有这些符号，用它会编译失败：

```
error: 'GLFW_SOFT_FULLSCREEN' was not declared in this scope
```

因此构建脚本**强制** `-DUSE_SYSTEM_GLFW=OFF`，并会先校验子模块里
确实含 `GLFW_SOFT_FULLSCREEN`，否则直接退出并提示 clone 方法。

（本文档早期版本写的是 `-DUSE_SYSTEM_GLFW=ON`「用系统库」，
那是错的，已改正。）

### 2. 音频后端

Linux 上 mpv 的后端设置为：

```
pulse,pipewire,alsa
```

按顺序回退。掌机上 pipwire 比 pulse 更常见，放在前面也可以；
写死单一后端会在部分环境直接没有声音（Android 上踩过这个坑）。

### 3. 窗口尺寸

Steam Deck 是 **1280×800（16:10）**。项目不假设屏幕尺寸，
布局按逻辑 1280×906 等比缩放，实测 640×360 / 1024×600 / 1280×720 /
1280×800 / 1920×1080 五种分辨率布局形状一致。

调试时可用环境变量指定：

```bash
MUVIK_WINDOW=1280x800 ./build-linux/muvik
```

### 4. 验证方法

没有 Steam Deck 实机时，可以做的检查（**都不等于「能跑」**）：

```bash
# 1) 有没有未定义符号（链接是否完整）
docker run --rm -v "$PWD:/src" -w /src muvik-linux \
  bash -c 'ldd -r build-steamos/yiufplayer 2>&1 | grep -c "undefined symbol"'
# 期望 0

# 2) 直接依赖哪些库（跨发行版兼容性看这个）
docker run --rm -v "$PWD:/src" -w /src muvik-linux \
  bash -c 'objdump -p build-steamos/yiufplayer | grep NEEDED'

# 3) 需要的最高 glibc 版本（SteamOS 3.x 是 2.38+，一般够）
docker run --rm -v "$PWD:/src" -w /src muvik-linux \
  bash -c 'objdump -T build-steamos/yiufplayer | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1'
```

**容器里跑不起来界面**：macOS arm64 跑 amd64 容器走 QEMU，Xvfb 的 GLX
不完整，会崩在 `glfw: failed to initialize`（退出码 134）。
这是环境限制，不是代码问题 —— 但也意味着**渲染、手柄、触屏、硬解、
休眠唤醒在 Linux 上一个都没验证过**。真正的验证只能上实机。

### 5. 部署到 Deck 的两种方式

| 方式 | 脚本 | 适用 |
| :--- | :--- | :--- |
| 推源码，在 Deck 上编译 | `scripts/deploy-deck.sh` | 你在开发、想快速迭代 |
| 推编译好的发布包 | `scripts/package-steamos.sh` + scp | 给别人用 / 不想在 Deck 上装编译工具链 |

`deploy-deck.sh` 的设计理由：Deck 本身就是 x86_64 Linux，在它上面编译能
100% 对齐 Arch + AMD Mesa + 真实硬解，这是容器做不到的。
它**只同步源码**（rsync 增量 < 1 秒），在设备上增量编译（配 ccache 约 10-30 秒）：

```bash
DECK_HOST=deck@steamdeck.local ./scripts/deploy-deck.sh --run
```

前置（Deck 上执行一次）：

```bash
sudo steamos-readonly disable
sudo pacman -S --needed base-devel cmake ninja pkgconf git rsync \
     mpv ffmpeg mesa libcurl fmt tinyxml2 dbus
sudo steamos-readonly enable
# 推荐 distrobox，系统更新不会重置：
distrobox create -n deckdev -i archlinux:latest && distrobox enter deckdev
```
