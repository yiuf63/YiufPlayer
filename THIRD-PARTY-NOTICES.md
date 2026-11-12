# 第三方组件声明 / Third-Party Notices

**Muvik 自身以 GNU GPL-3.0 分发**（全文见根目录 `LICENSE`；
2026-10 之前是闭源专有，本文件的对应表述已随之更新）。

本文件列出随本产品一同分发的开源组件及其许可证，并说明合规义务的履行方式。

**最后更新：随每次发布构建更新。**

---

## 1. libmpv / mpv（LGPL-2.1-or-later）

- **用途**：音视频解码与渲染内核。
- **许可证**：GNU Lesser General Public License, version 2.1 or later。
- **合规方式**：**动态链接**。Muvik 通过 `libmpv.2.dylib`（macOS）/
  `libmpv.so.2`（Linux）在运行时加载，不将 mpv 代码静态编译进本产品可执行文件。
  用户可自由使用自行编译或修改过的 libmpv 替换随附版本，本产品不对该动态库做
  任何完整性校验或签名限制。
- **许可证全文**：见 `licenses/LICENSE-LGPL-2.1-mpv.txt`。
- **源码获取**：https://github.com/mpv-player/mpv
- **关于 GPL/LGPL 构建**：本项目是 GPL-3.0，因此链接 **GPL 或 LGPL 配置的
  libmpv / FFmpeg 都合法**。发布时须在本文件如实记录实际链接的是哪一种，
  并按 GPL-3.0 §6 提供对应源码（见「发布检查清单」）。

## 2. FFmpeg（经 libmpv 间接依赖，视构建而定）

- **用途**：由 libmpv 调用的解复用与编解码库。
- **许可证**：取决于构建参数，常见为 **GPL-3.0-or-later**
  （Homebrew / 多数发行版）或 LGPL（自行 `--disable-gpl` 构建时）。
- **合规要求**：GPL-3.0 之下二者均可分发；但**必须如实声明实际链接的构建**，
  因为分发义务（提供对应源码）要按实际依赖履行。
- **实测方式**：`python3 tests/tools/license_audit.py`（调用 `avutil_license()`，
  不要用 `strings` 抓 "GPL version" 字面量 —— FFmpeg 源码里也有，会误判）。
- **源码获取**：https://ffmpeg.org/

## 3. Borealis（Apache-2.0）

- **用途**：UI 框架（`external/borealis`）。
- **许可证**：Apache License 2.0。
- **合规方式**：保留原始许可证与版权声明，见
  `licenses/LICENSE-Apache-2.0-borealis.txt`。
- **修改声明（Apache-2.0 §4(b)）**：本项目对 Borealis 源码作出了修改，
  清单见 `external/borealis/PATCHES.md`。

## 4. libcurl（MIT/X 风格）

- **用途**：为插件系统提供 HTTP 请求能力（`muvik_host_api::http_request`）。
- **许可证**：MIT/X 派生许可，允许商业使用与闭源分发，仅需保留版权声明。
- **合规方式**：动态链接；许可证全文见 `licenses/LICENSE-curl-MIT.txt`。
- **源码获取**：https://curl.se/

## 5. QuickJS（MIT）

- **用途**：为插件系统提供轻量级 JavaScript 脚本执行环境与沙箱运行时（`external/quickjs`）。
- **许可证**：MIT License（Copyright (c) 2019-2024 Fabrice Bellard and Charlie Gordon）。
- **合规方式**：源码级静态编译，保留原始版权声明，见 `licenses/LICENSE-MIT-quickjs.txt`。
- **源码获取**：https://bellard.org/quickjs/

## 6. 其它随 Borealis 分发的组件

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

## 构建期许可证信息采集（不是防线）

构建时会编译一个极小探针（`cmake/license_probe.cpp`），调用 FFmpeg 官方的
`avutil_license()` 获取权威判定，而非依赖配置字符串匹配。它**只打印信息、
不会中止构建**——本项目是 GPL-3.0，链接 GPL 版 FFmpeg 合法。

实测输出：

```
==============================================================
 依赖许可证信息（用于发布时的源码与声明核对）
==============================================================
 avutil 版本 : 3998053
 许可        : GPL version 3 or later
 类别        : GPL（含 --enable-gpl 组件）
--------------------------------------------------------------
 本项目以 GPL-3.0 分发，链接 GPL 版 FFmpeg 合法。
 分发时须遵守 GPL-3.0 §6：提供对应源码。
==============================================================
```

保留它的原因：GPL-3.0 §6 要求分发二进制时提供**对应源码**，
所以必须能查出产物实际链接的是哪个 FFmpeg 构建。

## 可选：构建 LGPL 版 libmpv

**这一步不是发布前提**（GPL-3.0 之下不是），只在希望产物不带
`--enable-gpl` 组件时才做。

```bash
./scripts/build-lgpl-libmpv.sh ~/.local/muvik-lgpl
```

脚本会：
1. 下载并构建 FFmpeg，严格使用 `--disable-gpl --disable-version3 --disable-nonfree`
2. **构建后立即用 `avutil_license()` 复验**，若仍为 GPL 则中止（避免白忙）
3. 以 `-Dgpl=false` 构建 libmpv（仅 libmpv，不编 cplayer）
4. 输出 CMake 使用方式

构建完成后：

```bash
cmake -B build -DMPV_ROOT=$HOME/.local/muvik-lgpl \
      -DCMAKE_PREFIX_PATH=$HOME/.local/muvik-lgpl
cmake --build build
```

若 `MPV_ROOT` 指向无效路径，CMake 会**明确警告**「已回退到系统 mpv」，
避免在用户不知情时链接到与预期不同的构建。

## 发布检查清单

发布前逐项确认：

- [ ] **Muvik 自身源码可获取**（GPL-3.0 §6）：公开仓库，或随二进制附完整源码
- [ ] 已用 `python3 tests/tools/license_audit.py` 记录产物实际链接的 FFmpeg 构建，
      并在本文件第 2 节如实声明（GPL 或 LGPL）
- [ ] libmpv 以动态库形式随包分发，未被静态链接进主程序
- [ ] `LICENSE`（GPL-3.0 全文）与 `licenses/` 目录随安装包一同分发
- [ ] 未对 libmpv 动态库施加签名校验或替换限制
- [ ] 安装包内可见本文件
- [ ] 对 Borealis 的修改已在 `external/borealis/PATCHES.md` 声明（Apache-2.0 §4(b)）
