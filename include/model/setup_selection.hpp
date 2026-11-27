#pragma once

// 首次建库的目录勾选逻辑（纯函数，不依赖 borealis）。
//
// 抽出来的原因：这部分有两个真实边界，靠「界面上有个方块」是测不出来的。
//
//   1. **默认全选**：存的是「被排除的目录」而不是「被选中的目录」。
//      如果存选中列表，将来新增一个探测位置时它不会自动出现在列表里 ——
//      用户升级后发现某个标准目录没被导入，却不知道为什么。
//
//   2. **探测结果变化后，排除项要清洗**：用户上次取消勾选过
//      `/mnt/SDCARD`（拔卡了），这次探测不到它；插回卡后它重新出现，
//      此时应当**重新默认选中**，而不是沿用一条看不见的旧排除记录 ——
//      否则用户会看到「明明插着卡，却怎么都导不进去」。
//
// 这两条都是「状态与界面不同步」类缺陷，写在这里就能被测到。

#include <set>
#include <string>
#include <vector>

namespace muvik {

// 一次切换：在排除集合里翻转某一项。
inline void toggleExcluded(std::set<std::string>& excluded, const std::string& path) {
    if (path.empty()) return;
    auto it = excluded.find(path);
    if (it == excluded.end()) excluded.insert(path);
    else excluded.erase(it);
}

// 该目录当前是否被勾选（是否应当导入）。
inline bool isSelected(const std::set<std::string>& excluded, const std::string& path) {
    return excluded.find(path) == excluded.end();
}

// 统计勾选了几个。
inline size_t countSelected(const std::set<std::string>& excluded,
                            const std::vector<std::string>& paths) {
    size_t n = 0;
    for (const auto& p : paths) {
        if (isSelected(excluded, p)) ++n;
    }
    return n;
}

// 丢弃「本次探测结果里不存在」的排除记录，返回被清理掉的条目数。
//
// 调用时机：每次重新探测之后、渲染列表之前。
inline size_t pruneExcluded(std::set<std::string>& excluded,
                            const std::vector<std::string>& detected) {
    std::set<std::string> keep;
    for (const auto& p : detected) keep.insert(p);

    size_t removed = 0;
    for (auto it = excluded.begin(); it != excluded.end();) {
        if (keep.count(*it)) {
            ++it;
        } else {
            it = excluded.erase(it);
            ++removed;
        }
    }
    return removed;
}

} // namespace muvik
