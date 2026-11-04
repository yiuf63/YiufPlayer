# 测试

## 快速开始

```bash
# 运行全部单元测试
./tests/run_tests.sh

# 只看某个模块
./tests/run_tests.sh parser

# 显示详细输出（默认只显示汇总）
./tests/run_tests.sh -v
```

或通过 ctest（需先配置）：

```bash
cmake -B build -DBUILD_TESTING=ON
cd build && ctest --output-on-failure
```

## 为什么自建测试框架

没有引入 gtest / catch2 / doctest。原因：

1. **减少依赖**：第三方库越少，构建与许可证合规负担越轻。
2. **需求简单**：现有测试都是「断言 + 打印」形态，`tests/test_framework.hpp`
   约 120 行即可覆盖。
3. **构建轻量**：测试是独立可执行文件，编译快、无链接负担。

如果将来测试规模显著扩大（需要 fixture、参数化、mock），再引入成熟框架也不迟。

## 目录结构

```
tests/
├── test_framework.hpp          # 断言宏与汇总
├── run_tests.sh                # 统一编译与运行入口
└── unit/
    ├── test_media_name_parser.cpp   # 文件名 -> 刮削搜索词
    ├── test_metadata_store.cpp      # 元数据持久化与转义
    ├── test_media_library.cpp       # 媒体库扫描与过滤
    ├── test_plugin_installer.cpp    # ZIP 安装与安全隐患
    ├── test_plugin_config.cpp       # 插件配置持久化
    ├── test_playback_history.cpp    # 播放进度持久化与并发
    ├── test_scraper.cpp             # 插件加载与刮削（含本地 WebDAV 服务）
    └── test_concurrency.cpp         # 并发安全
```

共 **8 个套件**（7 个在 `run_tests.sh` 的 `TESTS` 数组里登记，
`plugin_installer` 因需要 fmt/borealis 头文件而单独编译）。

## 编写新测试

```cpp
#include "your_module.hpp"
#include "test_framework.hpp"

int main() {
    TEST_BEGIN("模块名 / 场景");
    CHECK_TRUE(condition, "描述");
    CHECK_EQ(actual, expected, "描述");
    CHECK_CONTAINS(text, needle, "描述");
    TEST_END();
    return TEST_SUMMARY();
}
```

然后在 `tests/run_tests.sh` 的 `TESTS` 数组登记：

```
"名称|测试源文件|附加源文件|额外编译参数"
```

## 测试覆盖的重点

| 模块 | 重点场景 |
| :--- | :--- |
| `media_name_parser` | 各类影视命名、中文、边界（空串/短数字/超范围年份） |
| `metadata_store` | **换行转义回归**、特殊字符、多记录隔离、跨实例持久化 |
| `media_library` | 递归扫描、隐藏文件与非媒体过滤、外挂字幕匹配、移除清理 |
| `plugin_installer` | **Zip Slip 攻击**、越界卸载、无库包拒绝、同名不覆盖 |
| `concurrency` | 后台扫描与 UI 读取并行、元数据多线程写入、保存/读取交错 |
| `plugin_config` | 插件配置读写、特殊字符转义、多插件隔离、JSON 导出、持久化往返 |
| `playback_history` | 进度记录的写入/读取、边界（过短不记、看完清除）、并发安全 |
| `scraper` | 插件加载与 ABI 校验、刮削请求串联；若本地能起 WebDAV 测试服务则额外验证目录浏览 |

后两项涉及安全边界，测试会构造**真实的恶意 ZIP 包**来验证防护生效，
而不是仅检查代码里有没有某个 `if`。

## 关于回归测试

`metadata_store` 的换行用例来自一次真实 bug：简介中含换行时，
记录被写成两行，加载时因字段数不足而被整条丢弃。该用例会持续防止复发。

验证测试本身有效的方法：故意改坏源码，确认测试失败。例如移除
`media_name_parser.cpp` 里的扩展名剥离保护，`SomeShowWithoutExtension.2018`
用例应当立即红掉。

## 并发与内存检查

### 已验证

用 **AddressSanitizer + UndefinedBehaviorSanitizer** 运行全部测试与主程序：

```bash
c++ -std=c++17 -I include -I tests -fsanitize=address,undefined -g \
    -o /tmp/t tests/unit/<suite>.cpp <sources>
/tmp/t
```

结果：8 个测试套件、主程序 12 秒运行，**均无 ASan/UBSan 报告**。

`concurrency` 套件在 ASan 下完成约 1.7 万次并发读取，未出现崩溃或快照不一致。

### 未验证（重要）

**ThreadSanitizer 在本项目的 macOS 环境下不可用。**

尝试运行 TSAN 时，即使是最简单的故意竞态探针也会超时且不产生报告：

```bash
# 探针：两个线程同时 g = g + 1
c++ -fsanitize=thread ... && ./probe   # 超时，0 报告
```

这意味着 TSAN 的输出**不能作为「无数据竞争」的证据**（工具没跑起来，
不是没有竞争）。在 Linux / Steam Deck 上 TSAN 通常可用，应在那里补做。

当前并发正确性是靠以下方式保障的：
- 人工审查：所有共享状态由 `std::mutex` / `std::recursive_mutex` 保护
- `getItems()` 等返回**值拷贝**而非引用，调用方不会持有内部数据
- 后台线程只通过 `brls::sync` 回主线程，不直接触碰 UI

### 已知的调用方约束

跨两次调用比较语义不成立。例如：

```cpp
auto items  = lib.getItems();   // 快照 A
auto videos = lib.getVideos();  // 快照 B —— 后台扫描可能已替换数据集
if (videos.size() > items.size()) { /* 可能误判，不是 bug */ }
```

需要一致性视图时，应在**单次调用返回的容器内**完成全部判断。

## 测试工具

| 工具 | 用途 |
| :--- | :--- |
| `tests/tools/dav_test_server.py` | 最小 WebDAV 服务端，用于验证 WebDAV 插件 |

用法：

```bash
python3 tests/tools/dav_test_server.py <根目录> [端口] [用户名] [密码]
```

它只实现 PROPFIND（Depth: 1）与 Basic 鉴权——足够验证插件，
不实现任何写操作。
