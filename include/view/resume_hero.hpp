#pragma once

// 首页顶部的「继续观看」主视觉。
//
// 解决什么问题：
//   修复前首页首焦点是第一个文件夹，上次看的那一集藏在「最近添加」第四行、
//   靠近底栏（交接文档 01-findings.md 实测）。用户打开播放器最想做的事
//   是「接着看」，却要先找到文件夹、进去、翻到那一集。
//
// 设计约束（都是项目实际情况，不是偏好）：
//   1. **必须有无图状态**。SteamOS 的 thin 包默认关闭抽帧，首批内侧机
//      TrimUI 也走 thin。把大封面当必需品的方案在这两个包上是空的。
//      无图时收成一条紧凑行，把宽度还给片名。
//   2. **必须显示真实季集号**。缺集时（S01E05/E01E08）用下标会写成
//      「第 2 集」，用户以为文件放错了。剧集页已有 episodeOf()，这里复用。
//   3. **文案与行为一致**。只有 canResume() 为真才写「继续播放」并给
//      播放进度；否则写「最近打开」并说明从头播。首页承诺什么，
//      按 A 就必须发生什么（fixture: resume-eligibility）。

#include <chrono>
#include <string>

#include <borealis.hpp>

#include "model/media_item.hpp"
#include "ui/scene_tile.hpp"

namespace muvik {

// 首页主视觉需要的最小信息集。
//
// 不从 MediaItem 里读进度：进度在 PlaybackHistory 里，而 MediaItem
// 是扫描产物、不含播放状态。调用方负责组装好再传进来。
struct ResumeHeroSpec {
    MediaItem item;
    std::string title;        // 展示标题（刮削优先）
    std::string series_title; // 剧集名；空表示不是剧集
    int season = 0;           // 真实季号；0 表示未知
    int episode = 0;          // 真实集号；0 表示未知
    int64_t position = 0;     // 上次停留（秒）
    int64_t duration = 0;     // 总时长（秒）
    bool can_resume = false;  // canResume() 的结果，决定文案与动作
};

class ResumeHero : public brls::Box {
public:
    // 有封面时的整卡高度。无图时会自动降到 COMPACT_H。
    static constexpr float HEIGHT = 208.0f;
    static constexpr float COMPACT_H = 96.0f;

    explicit ResumeHero(ResumeHeroSpec spec);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    // 无图时用紧凑行，调用方据此设置自身高度。
    bool isCompact() const { return !has_art_; }

    // 焦点过渡（与 MediaCell 同一套做法：按墙钟插值，不引 tween 库）
    void setFocused(bool focused);

    void setIdentity(std::string id) { spec_.item.path = std::move(id); }
    const std::string& identity() const { return spec_.item.path; }

private:
    ResumeHeroSpec spec_;
    ui::SceneSpec art_;
    bool has_art_ = false;

    float focus_anim_ = 0.0f;
    bool focused_ = false;
    std::chrono::steady_clock::time_point focus_t0_{};
};

} // namespace muvik
