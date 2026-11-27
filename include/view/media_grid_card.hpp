#pragma once

// 平铺网格里的一张卡：16:9 缩略图 + 片名 + 元信息 + 续播进度。
//
// 为什么不复用 MediaCell：媒体库是双层结构（文件夹 → 条目），
// 「全部平铺」这一层是**墙**，靠缩略图认片；列表行只有 76px 高，
// 缩略图缩到 96×54 之后什么都认不出。
//
// 尺寸：一行四张，卡片自己左右各让 8（缝由卡片让出来，容器不管间距）。

#include <string>

#include <borealis/core/box.hpp>

#include "model/media_item.hpp"
#include "ui/icons.hpp"

namespace muvik {

class MediaGridCard : public brls::Box {
public:
    static constexpr float HEIGHT = 200.0f;

    MediaGridCard(MediaItem item, std::string title, std::string subtitle,
                  float progress = -1.0f);

    void setTitle(std::string title);
    void setSubtitle(std::string subtitle);
    void setProgress(float ratio) { progress_ = ratio; }

    // 这一张代表哪个条目（媒体绝对路径）。用于重建后按身份恢复焦点。
    void setIdentity(std::string id) { identity_ = std::move(id); }
    const std::string& identity() const { return identity_; }
    // 右上角的短标签（「已看完」/「★ 8.2」）
    void setBadge(std::string badge) { badge_ = std::move(badge); }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

private:
    std::string identity_;
    MediaItem item_;
    std::string title_;
    std::string subtitle_;
    std::string badge_;
    float progress_ = -1.0f;
};

} // namespace muvik
