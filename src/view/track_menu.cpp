#include <functional>
#include <string>
#include <vector>
#include "view/track_menu.hpp"
#include <borealis/views/dialog.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/label.hpp>

namespace deck_player {

void TrackSelectionDialog::showSubtitleDialog(
    const std::vector<TrackInfo>& sub_tracks,
    float current_delay,
    std::function<void(int track_id)> on_select_track,
    std::function<void(float delay)> on_adjust_delay,
    std::function<void()> on_load_external
) {
    brls::Box* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setWidth(460);

    brls::Header* header = new brls::Header();
    header->setTitle("字幕");
    container->addView(header);

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setHeight(340);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);

    brls::Dialog* dialog = new brls::Dialog(container);

    // 1. 关闭字幕
    brls::DetailCell* none_cell = new brls::DetailCell();
    none_cell->setText("关闭字幕");
    none_cell->setDetailText("不显示任何字幕轨道");
    none_cell->registerClickAction([dialog, on_select_track](brls::View* view) {
        (void)view;
        if (on_select_track) on_select_track(0);
        dialog->close();
        return true;
    });
    list->addView(none_cell);

    // 2. 按「内嵌 / 外挂」分组列出
    auto build_group = [&](const std::string& group_title,
                           const std::vector<const TrackInfo*>& group) {
        if (group.empty()) return;
        brls::Header* gh = new brls::Header();
        gh->setTitle(group_title + " (" + std::to_string(group.size()) + ")");
        list->addView(gh);

        for (const TrackInfo* t : group) {
            brls::DetailCell* cell = new brls::DetailCell();
            cell->setText((t->selected ? "● " : "○ ") + t->displayName());

            // 详情：格式 + 是否位图 + 当前状态
            std::string detail;
            if (!t->codec.empty()) {
                if (t->is_bitmap()) {
                    detail = "图形字幕 (PGS/SUB) · 保持原画质";
                } else if (t->codec == "ass" || t->codec == "ssa") {
                    detail = "ASS 特效字幕 · 原生渲染";
                } else {
                    detail = t->codec + " 文本字幕";
                }
            }
            if (t->selected) {
                detail += detail.empty() ? "当前正在使用" : " · 当前正在使用";
            }
            cell->setDetailText(detail);

            int tid = t->id;
            cell->registerClickAction([dialog, on_select_track, tid](brls::View* view) {
                (void)view;
                if (on_select_track) on_select_track(tid);
                dialog->close();
                return true;
            });
            list->addView(cell);
        }
    };

    std::vector<const TrackInfo*> embedded, external;
    for (const auto& t : sub_tracks) {
        if (t.external) external.push_back(&t);
        else embedded.push_back(&t);
    }

    if (sub_tracks.empty()) {
        brls::DetailCell* empty = new brls::DetailCell();
        empty->setText("未检测到字幕轨道");
        empty->setDetailText("该文件没有内嵌字幕，也不存在同名外挂字幕");
        list->addView(empty);
    } else {
        build_group("内嵌字幕", embedded);
        build_group("外挂字幕", external);
    }

    // 3. 加载外部字幕文件
    if (on_load_external) {
        brls::DetailCell* load_cell = new brls::DetailCell();
        load_cell->setText("加载外部字幕文件…");
        load_cell->setDetailText("支持 .srt / .ass / .vtt / .sup 等格式");
        load_cell->registerClickAction([dialog, on_load_external](brls::View* view) {
            (void)view;
            dialog->close();
            if (on_load_external) on_load_external();
            return true;
        });
        list->addView(load_cell);
    }

    // 4. 字幕延时微调
    brls::Header* dh = new brls::Header();
    dh->setTitle("时间轴微调");
    list->addView(dh);

    brls::DetailCell* delay_minus = new brls::DetailCell();
    delay_minus->setText("字幕提前 0.5 秒");
    delay_minus->registerClickAction([current_delay, on_adjust_delay](brls::View* view) {
        (void)view;
        if (on_adjust_delay) on_adjust_delay(current_delay - 0.5f);
        brls::Application::notify("字幕已提前 0.5 秒");
        return true;
    });
    list->addView(delay_minus);

    brls::DetailCell* delay_plus = new brls::DetailCell();
    delay_plus->setText("字幕延后 0.5 秒");
    delay_plus->registerClickAction([current_delay, on_adjust_delay](brls::View* view) {
        (void)view;
        if (on_adjust_delay) on_adjust_delay(current_delay + 0.5f);
        brls::Application::notify("字幕已延后 0.5 秒");
        return true;
    });
    list->addView(delay_plus);

    scroller->setContentView(list);
    container->addView(scroller);

    dialog->addButton("返回", []() {});
    dialog->open();
}

void TrackSelectionDialog::showAudioDialog(
    const std::vector<TrackInfo>& audio_tracks,
    std::function<void(int track_id)> on_select_track
) {
    brls::Box* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setWidth(460);

    brls::Header* header = new brls::Header();
    header->setTitle("音轨");
    container->addView(header);

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setHeight(300);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);

    brls::Dialog* dialog = new brls::Dialog(container);

    if (audio_tracks.empty()) {
        brls::Label* empty_label = new brls::Label();
        empty_label->setText("未检测到可切换的音轨");
        empty_label->setMarginTop(20);
        empty_label->setMarginLeft(20);
        list->addView(empty_label);
    } else {
        for (const auto& track : audio_tracks) {
            brls::DetailCell* cell = new brls::DetailCell();
            cell->setText((track.selected ? "● " : "○ ") + track.displayName());

            std::string detail = track.codec.empty() ? "" : track.codec;
            if (track.selected) {
                detail += detail.empty() ? "当前正在播放" : " · 当前正在播放";
            }
            cell->setDetailText(detail);

            int tid = track.id;
            cell->registerClickAction([dialog, on_select_track, tid](brls::View* view) {
                (void)view;
                if (on_select_track) on_select_track(tid);
                dialog->close();
                return true;
            });
            list->addView(cell);
        }
    }

    scroller->setContentView(list);
    container->addView(scroller);

    dialog->addButton("返回", []() {});
    dialog->open();
}

} // namespace deck_player
