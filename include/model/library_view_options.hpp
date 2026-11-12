#pragma once

// 媒体库的显示规则。
//
// 为什么把这些选项抽成独立对象：
//   上一版把「怎么排、显示哪一类」写死在渲染里（名称序、视频段 + 音乐段），
//   用户没有任何入口。但真正的问题不是「缺一个按钮」——是把 5 个文件夹
//   的所有条目拉平成一条列表时，一屏 16 个条目怎么排都是乱的。
//   MX Player 的做法是把显示规则收进一个单独呼出的菜单：入口唯一、
//   里面可以分组。这个类就是那个菜单的数据模型，含持久化。
//
// 「按文件夹 / 全部平铺」不是排序，是结构 —— 它决定列表是两层还是一层。

#include <string>
#include <vector>

#include "model/media_item.hpp"

namespace muvik {

enum class LibraryGroup {
    Folder,   // 先看文件夹卡片，进去再看内容
    Flat,     // 全部平铺（视频段 + 音乐段）
};

enum class LibraryContent {
    All,
    VideoOnly,
    AudioOnly,
};

// 排序键。
//
// 原型的第四项是「时长」，但扫描器不解析媒体时长（`MediaItem::duration`
// 目前恒为 0）。给一个永远相等的排序键，点下去界面上什么都不会变，
// 用户只会以为菜单坏了。所以换成「播放进度」——历史记录里有真实数据，
// 也是「把没看完的挑出来」这个真实需求的正解。抽帧 / 探测管线接上之后
// 可以再加回时长。
enum class LibrarySort {
    Name,
    Recent,     // 文件修改时间，新的在前
    Size,
    Progress,   // 播放进度
};

enum class LibraryFilter {
    All,
    Unfinished, // 看过但没看完
    Finished,
};

struct LibraryViewOptions {
    LibraryGroup group = LibraryGroup::Folder;
    LibraryContent content = LibraryContent::All;
    LibrarySort sort = LibrarySort::Name;
    bool descending = false;
    LibraryFilter filter = LibraryFilter::All;

    // 全进程唯一实例，首次访问时从磁盘读取
    static LibraryViewOptions& get();

    void load();
    void save() const;
    void reset();

    // 工具行入口上那句摘要，如「按文件夹 · 名称」
    std::string summary() const;

    // 是否需要「最近添加」这一段：只在按文件夹结构、无搜索、无筛选时出现。
    // 平铺视图里再插一段「最近添加」就是同一批条目出现两次。
    bool showsRecent() const;

    // 筛 + 排。搜索串非空时按标题子串过滤（大小写不敏感）。
    std::vector<MediaItem> apply(std::vector<MediaItem> items,
                                 const std::string& query = std::string()) const;

    bool matches(const MediaItem& item) const;
};

// 自然序比较：「02. 沙丘」排在「10. 沙丘」前面。
// 纯字典序会把 10 排到 2 前面，而媒体文件名几乎都是这种编号开头。
bool naturalLess(const std::string& a, const std::string& b);

} // namespace muvik
