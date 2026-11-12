# Android 构建

目标：把 Muvik 装到 Android 设备（平板、Android TV 盒子、掌机）。

## 前置条件

| 依赖 | 版本 | 说明 |
| :--- | :--- | :--- |
| Android SDK | API 34 | `~/Library/Android/sdk` |
| NDK | 28.2.13676358 | 必须与 `app/build.gradle` 的 `ndkVersion` 一致 |
| Java | 17+ | Gradle 要求 |
| SDL2 源码 | 2.30.9 | 见下 |

### SDL2 源码（必做）

borealis 的 SDL 是 git submodule，本仓库里**没有随附**。
Android 构建必须有它，否则 `external/borealis` 配置阶段就会失败：

```bash
cd external/borealis/library/lib/extern
git clone --depth 1 -b release-2.30.9 https://github.com/libsdl-org/SDL.git SDL
```

版本要与 `app/src/main/java/org/libsdl/app/` 里的 Java 桥接层匹配
（这批 Java 文件是从 SDL 2.30.x 的 Android 目录复制的）。

## 构建

```bash
cd android
./gradlew assembleDebug          # 产物：app/build/outputs/apk/debug/app-debug.apk
```

国内网络可能拉不到 Gradle 与 Android 插件，先确认能访问 `dl.google.com` 与 `repo.maven.apache.org`。

## 安装到设备

```bash
adb devices                      # 确认设备已连接
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

启动：

```bash
adb shell am start -n org.muvik.android/.MuvikActivity
```

看日志：

```bash
adb logcat -s Muvik SDL borealis
```

## 已知限制（首版）

| 项 | 状态 | 原因 |
| :--- | :--- | :--- |
| 本地视频播放 | 待验证 | 需要 Storage Access Framework 授权 |
| 网络源插件 | ❌ 不可用 | 未链接 libcurl（Android 上没有系统 curl） |
| 蓝牙遥控器 | 待验证 | 需要 `onKeyDown` 桥接到 SDL |
| 竖屏布局 | 待验证 | 见下 |

**竖屏**：项目界面按横屏设计。平板竖屏时布局会等比缩放，
但触控目标（按钮、列表项）可能偏小。真机上需要实测。

## 为什么不用 Android 原生 UI

UI 完全由 borealis 用 OpenGL ES 自绘，不使用任何 Android 控件。
好处是同一套代码在掌机/桌面/Android 上表现一致；
代价是无法使用系统控件（如原生文件选择器），需要自己实现。
