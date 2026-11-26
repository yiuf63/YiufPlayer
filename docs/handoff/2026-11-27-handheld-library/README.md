# Yiuf Player 掌机片库内测交接

下一个里程碑是让玩家用手柄完成四个动作：选定要扫描的目录，搜到刚才看到的影片，按一次 A 继续观看，按 B 回到原来的项目。产品沿用 **Yiuf Player**，默认媒体库采用已选定的 **影院蓝**。

评审基线为 `408b3f55a592c7613b43e641f7af79fbeac1b3cb`。2026-11-26 在 Mac arm64 临时副本重新构建，17 组单测、9 项原生操作冒烟通过。11 月 27 日复核了搜索、剧集归类和最近顺序。Steam Deck 与 TrimUI Smart Pro 实机结果待填。[原生验证记录](evidence/verification.txt)

| 接手时要做的事 | 文件 |
|---|---|
| 把任务原文发给执行 agent | [可直接转发的任务说明](START-HERE.md) |
| 了解当前缺陷、复现输入和源码落点 | [现状与复现](01-findings.md) · [可复用样例](fixtures/library-cases.json) |
| 实现首次建库、续播、无图布局及返回焦点 | [界面与交互](02-ux.md) · [原生现状与改版对照](evidence/home-comparison.png) |
| 选择第一批改动，了解依赖与交付顺序 | [实施排期](03-work-plan.md) |
| 执行回归、记录真机结果 | [验收清单](04-acceptance.md) |
| 后续接入刮削候选、视频源实例和 NAS 错误恢复 | [来源与扩展](05-providers.md) |

**从第一批开始。** 用现有原生类固定搜索、最近顺序、缺集和续播资格四条规则；随后改默认入口 `renderLibraryOverview()`。同一目录内的 [HTML 原型](reference/cinema-blue.html) 可离线打开，用于参考影院蓝的视觉和操作。其工作名 CueHarbor、模拟数据和未来功能仅用于设计演示。实施以 Yiuf Player 的 C++17、Borealis、libmpv 及现有 C ABI / QuickJS 接口为准。

仓库约定见根目录 `AGENTS.md`。`web/ui-lab/` 是另存的视觉探索。交接基线时该目录为未跟踪的用户文件。
