#pragma once

// 媒体库首屏的文件夹卡片：四宫格拼贴 + 计数角标 + 名称。
//
// 为什么不是「一行一个文件夹」：
//   平铺 10 个「01.」「02.」开头的文件，一行一条，一屏只放得下 7 条，
//   用户扫不出「这几个是同一部剧」。文件夹是用户自己在文件管理器里分好的
//   桶，把桶画成卡片，一屏 8 个，比列表快得多。
//
// 为什么是四宫格而不是「取第一个文件当封面」：
//   单张封面会让这个目录长得像那一部片，用户会以为文件夹里只有它。
//   四格才读得出「这里装着一堆东西」。
//
// 四格的画面来源：
//   目前是「配色板 + 地平线 + 亮源」的占位画面（按路径哈希取色，同一文件
//   每次渲染恒定）。它看着像画面而不是色斑，是因为有一条硬边的地平线和
//   一个明确的亮源 —— 纯径向渐变会糊成一团雾。
//   抽帧管线落地后（mpv screenshot-to-file + 缓存），只需把 drawTile() 的
//   填充换成缩略图位图，卡片的尺寸、层级、交互都不用动。

#include <string>
#include <vector>

#include <borealis/core/box.hpp>

#include "model/media_item.hpp"

namespace deck_player {

class MediaFolderCard : public brls::Box {
public:
    // 卡片高 = 2 + 缩略区 148 + 文字区 58 + 底部留白 8。
    // 缩略区在 1280 宽、侧栏 160 时约 257×148，接近 16:9。
    static constexpr float HEIGHT = 216.0f;

    // covers 最多取 4 个，用来铺四宫格；不足 4 个的位置留空，
    // 不复制已有条目来「补齐」—— 复制出来的画面会让用户以为有两部片。
    MediaFolderCard(std::string name, std::string path,
                    std::vector<MediaItem> covers,
                    int video_count, int audio_count);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    const std::string& path() const { return path_; }

private:
    std::string name_;
    std::string path_;
    std::vector<MediaItem> covers_;
    int videos_ = 0;
    int audios_ = 0;
};

} // namespace deck_player
