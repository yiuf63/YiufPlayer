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

## 为什么不用交叉编译

macOS → Linux 交叉编译需要目标平台的 `libmpv`、`libGL`、X11 等预编译库。
这些在容器里天然就有，所以用容器比搭交叉工具链省事得多。

## 已知注意事项

### 1. 子模块未随附

`external/borealis/library/lib/extern/{glfw,SDL}` 是 git submodule，
本仓库里是空目录。构建脚本因此显式使用系统库：

```bash
-DUSE_SYSTEM_GLFW=ON      # GLFW 用系统的
```

**不要**试图让它编 submodule 里的版本 —— 空目录会直接导致配置失败。

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

没有 Steam Deck 实机时，可以用容器做**无窗口**的冒烟验证：

```bash
# 检查二进制是否正确链接了 libmpv
docker run --rm -v "$PWD:/src" -w /src muvik-linux \
  bash -c 'ldd build-linux/muvik | grep -E "mpv|avutil|glfw"'
```

有窗口的完整验证需要真实显示环境（Steam Deck 或带 X 的 Linux）。
