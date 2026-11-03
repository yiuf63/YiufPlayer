# 第三方组件声明 / Third-Party Notices

DeckPlayer 自身源代码为专有软件（Proprietary）。本文件列出随本产品一同分发的开源组件
及其许可证，并说明合规义务的履行方式。

**最后更新：随每次发布构建更新。**

---

## 1. libmpv / mpv（LGPL-2.1-or-later）

- **用途**：音视频解码与渲染内核。
- **许可证**：GNU Lesser General Public License, version 2.1 or later。
- **合规方式**：**动态链接**。DeckPlayer 通过 `libmpv.2.dylib`（macOS）/
  `libmpv.so.2`（Linux）在运行时加载，不将 mpv 代码静态编译进本产品可执行文件。
  用户可自由使用自行编译或修改过的 libmpv 替换随附版本，本产品不对该动态库做
  任何完整性校验或签名限制。
- **许可证全文**：见 `licenses/LICENSE-LGPL-2.1-mpv.txt`。
- **源码获取**：https://github.com/mpv-player/mpv
- **重要前提**：本产品的发布构建 MUST 使用 `-Dgpl=false` 配置的 libmpv，
  且其依赖的 FFmpeg MUST 以 LGPL 配置构建（`--disable-gpl --disable-nonfree`）。
  使用 GPL 构建的 libmpv/FFmpeg 分发将触发 GPL 传染义务，**不得用于发布**。

## 2. FFmpeg（经 libmpv 间接依赖，LGPL-2.1-or-later）

- **用途**：由 libmpv 调用的解复用与编解码库。
- **合规要求**：同上，发布构建必须使用 LGPL 配置的 FFmpeg。
- **源码获取**：https://ffmpeg.org/

## 3. Borealis（Apache-2.0）

- **用途**：UI 框架（`external/borealis`）。
- **许可证**：Apache License 2.0。
- **合规方式**：保留原始许可证与版权声明，见
  `licenses/LICENSE-Apache-2.0-borealis.txt`。
- **修改声明（Apache-2.0 §4(b)）**：本项目对 Borealis 源码作出了修改，
  清单见 `external/borealis/PATCHES.md`。

## 4. libcurl（MIT/X 风格）

- **用途**：为插件系统提供 HTTP 请求能力（`deck_host_api::http_request`）。
- **许可证**：MIT/X 派生许可，允许商业使用与闭源分发，仅需保留版权声明。
- **合规方式**：动态链接；许可证全文见 `licenses/LICENSE-curl-MIT.txt`。
- **源码获取**：https://curl.se/

## 5. 其它随 Borealis 分发的组件

| 组件 | 许可证 | 位置 |
| :--- | :--- | :--- |
| {fmt} | MIT | `external/borealis/library/lib/extern/fmt` |
| GLFW | zlib/libpng | `external/borealis/library/lib/extern/glfw` |
| NanoVG | zlib | `external/borealis/library/lib/extern/nanovg` |
| Yoga | MIT | `external/borealis/library/lib/extern/yoga` |
| tinyxml2 | zlib | `external/borealis/library/lib/extern/tinyxml2` |
| tweeny | MIT | `external/borealis/library/lib/extern/tweeny` |
| libromfs | MIT | `external/borealis/library/lib/extern/libromfs` |
| glad | MIT/Apache-2.0 | `external/borealis/library/lib/extern/glad` |

以上均为宽松许可证，允许闭源商业分发，仅需保留版权与许可证声明。

---

## 构建期许可证防线

本项目在 CMake 中内置了一道**强制的许可证检查**（`cmake/license_probe.cpp`）：

构建时会编译一个极小探针，调用 FFmpeg 官方的 `avutil_license()` 获取权威判定，
而非依赖配置字符串匹配。若检测到 GPL：

- **默认行为：直接中止构建**，并提示解决方案。
- 本地开发自测可用 `-DALLOW_GPL_LINKAGE=ON` 显式放行，
  此时会打印醒目警告说明「该产物不得分发」。

实测输出：

```
FFmpeg license: GPL version 3 or later
==============================================================
 构建中止：检测到 GPL 版 FFmpeg
==============================================================
```

这可以防止「不小心用 GPL 构建发布了闭源产品」这类高危失误。

## 构建 LGPL 版 libmpv

```bash
./scripts/build-lgpl-libmpv.sh ~/.local/deckplayer-lgpl
```

脚本会：
1. 下载并构建 FFmpeg，严格使用 `--disable-gpl --disable-version3 --disable-nonfree`
2. **构建后立即用 `avutil_license()` 复验**，若仍为 GPL 则中止（避免白忙）
3. 以 `-Dgpl=false` 构建 libmpv（仅 libmpv，不编 cplayer）
4. 输出 CMake 使用方式

构建完成后：

```bash
cmake -B build -DMPV_ROOT=$HOME/.local/deckplayer-lgpl \
      -DCMAKE_PREFIX_PATH=$HOME/.local/deckplayer-lgpl
cmake --build build
```

若 `MPV_ROOT` 指向无效路径，CMake 会**明确警告**「已回退到系统 mpv」，
避免在用户不知情时链接到 GPL 版本。

## 发布检查清单

发布前逐项确认：

- [ ] 发布用 libmpv 以 `-Dgpl=false` 构建
- [ ] 发布用 FFmpeg 无 `--enable-gpl` / `--enable-nonfree`
- [ ] libmpv 以动态库形式随包分发，未被静态链接进主程序
- [ ] `licenses/` 目录随安装包一同分发
- [ ] 未对 libmpv 动态库施加签名校验或替换限制
- [ ] 安装包内可见本文件
