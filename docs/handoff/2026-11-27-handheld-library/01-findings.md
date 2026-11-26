# 当前行为与复现

本文记录代码基线 `408b3f5`。源码路径相对于仓库根目录，行号用于定位该提交。历史测试的输入是合成媒体和临时配置。

| 事项 | 已观察到的行为 | 原生落点 |
|---|---|---|
| 搜索显示名 | `cam_0042.mp4` 的元数据片名为「蓝调时刻」。真实 C++ 过滤器搜片名得到 0 项，搜 `cam_0042` 得到 1 项。卡片优先画资料标题。 | `src/model/library_view_options.cpp:215` 的 `LibraryViewOptions::apply`；`src/activity/file_browser_activity.cpp:219` 的 `readVideoMeta` |
| 最近顺序 | 三条缓存记录按修改时间得到 `z_new → m_middle → a_old`，再应用默认视图选项后变为 `a_old → m_middle → z_new`。首次入库时间和文件修改时间当前未分列。 | `src/model/media_library.cpp:163` 的 `getRecent`，扫描读取 `last_write_time`；`src/activity/file_browser_activity.cpp:1367` 再调用 `apply` |
| 混合目录、缺集 | 同目录放 `Harbor.S01E05.mkv`、`Harbor.S01E08.mkv`、`RacingCapture.mp4`。识别器实测为 1 部剧、3 个分集、0 条独立视频。E08 的零基索引为 1。详情按钮的显示代码用索引加一，会标成「第 2 集」。标 E08 已完成后，模型仍可保留它作为最近观看项。 | `src/model/series_detector.cpp:168`、`:220`、`:225`；`src/activity/series_activity.cpp:106` |
| 续播承诺 | 同一条 45 秒原生样片，记录 12 秒时页面可显示已看 26%，播放端没有设置续播起点；18 秒时设置 `start=18`。列表、剧集页和打开视频的条件各异。 | `file_browser_activity.cpp` 的 `readVideoMeta`；`src/activity/series_activity.cpp:213`；`src/activity/player_activity.cpp` 的 `resume_pos`、`rec.position > 15` |
| 焦点恢复 | `openFolder()` 进入和退出都安排焦点到固定序号，并把滚动位置归零。缩略图到达会重建列表，重建后按旧序号恢复；排序变化时同一序号可能已是另一部片。 | `src/activity/file_browser_activity.cpp:1084`、`:1113`、`:1273`、`:1715` |
| 首次扫描 | 检测到常用位置时，「一键建立影院」显示目录数；确认后将 `detected` 全部加入。用户难以从文案判断有没有选中整张卡。 | `src/activity/file_browser_activity.cpp:77` 的 `detectLocalMediaSources`、`:1166` 的首次建库分支 |
| 默认首页 | 首焦点是「海港手记」文件夹。样片第二集及其 26% 进度在「最近添加」第四行，靠近底栏。默认分组为 Folder，走 `renderLibraryOverview()`。 | `include/model/library_view_options.hpp`；`src/activity/file_browser_activity.cpp:1256`、`:1285` |
| 窄窗与无图 | 640×360 原生截图仍保留侧栏、搜索、排序和四列目录。Mac 开抽帧时四个样片生成四张缓存图；关闭抽帧并使用空缓存时生成零张。仓库的 SteamOS `thin` 打包默认关闭抽帧。 | `file_browser_activity.cpp:1342`、`:1646`；`include/view/media_thumb.hpp:15`；`src/utils/thumbnail_cache.cpp:36`；`scripts/package-steamos.sh:29` |
| 刮削确认与网络失败 | 当前路径先取第一个刮削器、再取第一个候选 ID，弹「完成」前已经写入资料。WebDAV 访问失败时清空列表并显示错误，已有 Y 刷新。 | `file_browser_activity.cpp:2663`、`:2696`；`src/plugin/plugin_manager.cpp:750`；`src/activity/network_browser_activity.cpp:115` |

**证据的范围。** 首页和 640×360 图来自 Mac 原生程序，关闭抽帧图来自相同样片的 Mac 对照构建。搜索、排序和归类结果来自编译真实项目类的探针；错误集号来自探针索引与界面格式化代码的对应关系。对照图右半为改版建议，采用另一条可续播的 18 秒记录。[首次打开](evidence/first-run.png) · [有图总览](evidence/library-1280.png) · [小窗](evidence/library-small.png) · [无图总览](evidence/library-no-thumbs.png) · [验证摘录](evidence/verification.txt)
