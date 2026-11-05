#pragma once

// 「一条媒体该画什么」的唯一入口。
//
// 文件夹四宫格、平铺网格、列表缩略图三处都要这组参数：缩略图缓存路径、
// 占位色种子、是不是音频。各自拼一遍的结果是其中一处忘了查缓存，
// 于是同一部片在网格里是真实帧、在列表里是占位色块。

#include "model/media_item.hpp"
#include "ui/scene_tile.hpp"
#include "utils/thumbnail_cache.hpp"

namespace deck_player {

inline ui::SceneSpec sceneSpecFor(const MediaItem& item) {
    ui::SceneSpec spec;
    spec.thumb_path = ThumbnailCache::getInstance().filePath(item.path);
    spec.seed = ui::hashString(item.path);
    spec.audio = item.is_audio();
    return spec;
}

} // namespace deck_player
