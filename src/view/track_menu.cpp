#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "view/track_menu.hpp"
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include <borealis/views/dialog.hpp>
#include <borealis/views/scrolling_frame.hpp>

namespace deck_player {

namespace {

// 对话框里的一行 = 卡片行（比主界面矮一档，这样 6~7 条能一屏看完）。
constexpr float kDialogRowHeight = 64.0f;

void addRow(brls::Box* list, ui::Icon icon, const std::string& title,
            const std::string& subtitle, const std::string& badge,
            std::function<void()> on_click) {
    auto* cell = new MediaCell(icon, title, subtitle);
    cell->setHeight(kDialogRowHeight);
    if (!badge.empty()) cell->setBadge(badge);
    cell->registerClickAction([on_click](brls::View*) {
        if (on_click) on_click();
        return true;
    });
    list->addView(cell);
}

} // namespace

// 把内容高度算出来，让对话框只占需要的空间。
// 固定 420 高度时，3 条字幕会留下一大片空白黑块，观感像「面板坏了」。
float contentHeight(brls::Box* list, float max_h) {
    float h = 0.0f;
    for (auto* child : list->getChildren()) h += child->getHeight();
    return std::min(max_h, h + 24.0f);
}

void TrackSelectionDialog::showSubtitleDialog(
    const std::vector<TrackInfo>& sub_tracks,
    float current_delay,
    std::function<void(int track_id)> on_select_track,
    std::function<void(float delay)> on_adjust_delay,
    std::function<void()> on_load_external
) {
    brls::Box* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setWidth(620);

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setHeight(420);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPaddingBottom(20.0f);

    brls::Dialog* dialog = new brls::Dialog(container);
    list->addView(new SectionHeader("字幕 · " +
                                    std::to_string(sub_tracks.size()) + " 条轨道"));

    // 1. 关闭字幕
    addRow(list, ui::Icon::SubtitlesOff, "关闭字幕", "不显示任何字幕轨道", "关闭",
           [dialog, on_select_track]() {
               if (on_select_track) on_select_track(0);
               dialog->close();
           });

    // 2. 按「内嵌 / 外挂」分组列出
    auto build_group = [&](const std::string& group_title,
                           const std::vector<const TrackInfo*>& group) {
        if (group.empty()) return;
        list->addView(new SectionHeader(
            group_title + " · " + std::to_string(group.size())));

        for (const TrackInfo* t : group) {
            // 详情：格式 + 是否位图 + 当前状态
            std::string detail;
            if (t->codec == "hdmv_pgs_subtitle" || t->codec == "dvd_subtitle") {
                detail = "图形字幕 · 保持原画质";
            } else if (t->codec == "ass" || t->codec == "ssa") {
                detail = "ASS 特效字幕 · 原生渲染";
            } else if (!t->codec.empty()) {
                detail = t->codec + " 文本字幕";
            }
            if (!t->lang.empty()) {
                if (!detail.empty()) detail += " · ";
                detail += t->lang;
            }

            const int tid = t->id;
            addRow(list, t->is_bitmap() ? ui::Icon::Video : ui::Icon::Subtitles,
                   t->displayName(), detail, t->selected ? "使用中" : "",
                   [dialog, on_select_track, tid]() {
                       if (on_select_track) on_select_track(tid);
                       dialog->close();
                   });
        }
    };

    std::vector<const TrackInfo*> embedded, external;
    for (const auto& t : sub_tracks) {
        if (t.external) external.push_back(&t);
        else embedded.push_back(&t);
    }

    if (sub_tracks.empty()) {
        addRow(list, ui::Icon::Info, "没有找到字幕轨道",
               "该文件没有内嵌字幕，也不存在同名外挂字幕", "", nullptr);
    } else {
        build_group("内嵌字幕", embedded);
        build_group("外挂字幕", external);
    }

    // 3. 加载外部字幕文件
    if (on_load_external) {
        list->addView(new SectionHeader("其他"));
        addRow(list, ui::Icon::Plus, "加载外部字幕文件",
               "支持 .srt / .ass / .vtt / .sup 等格式", "",
               [dialog, on_load_external]() {
                   dialog->close();
                   if (on_load_external) on_load_external();
               });
    }

    // 4. 字幕延时微调
    list->addView(new SectionHeader("时间轴微调"));
    const std::string delay_desc =
        "当前偏移 " + std::to_string((int)(current_delay * 1000)) + " ms";
    addRow(list, ui::Icon::SeekBack, "整体提前 0.5 秒", delay_desc, "",
           [current_delay, on_adjust_delay]() {
               if (on_adjust_delay) on_adjust_delay(current_delay - 0.5f);
               brls::Application::notify("字幕已提前 0.5 秒");
           });
    addRow(list, ui::Icon::SeekForward, "整体延后 0.5 秒", delay_desc, "",
           [current_delay, on_adjust_delay]() {
               if (on_adjust_delay) on_adjust_delay(current_delay + 0.5f);
               brls::Application::notify("字幕已延后 0.5 秒");
           });

    scroller->setContentView(list);
    scroller->setHeight(contentHeight(list, 420.0f));
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
    container->setWidth(560);

    brls::ScrollingFrame* scroller = new brls::ScrollingFrame();
    scroller->setHeight(340);
    styleScrollingFrame(scroller);

    brls::Box* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPaddingBottom(20.0f);

    brls::Dialog* dialog = new brls::Dialog(container);
    list->addView(new SectionHeader("音轨 · " +
                                    std::to_string(audio_tracks.size()) + " 条"));

    if (audio_tracks.empty()) {
        addRow(list, ui::Icon::Info, "没有可切换的音轨",
               "当前文件只包含一条音频或不含音频", "", nullptr);
    } else {
        for (const auto& track : audio_tracks) {
            std::string detail = track.codec.empty() ? "音频轨道" : track.codec;
            if (!track.lang.empty()) detail += " · " + track.lang;
            const int tid = track.id;
            addRow(list, ui::Icon::Audio, track.displayName(), detail,
                   track.selected ? "使用中" : "",
                   [dialog, on_select_track, tid]() {
                       if (on_select_track) on_select_track(tid);
                       dialog->close();
                   });
        }
    }

    scroller->setContentView(list);
    scroller->setHeight(contentHeight(list, 340.0f));
    container->addView(scroller);

    dialog->addButton("返回", []() {});
    dialog->open();
}

} // namespace deck_player
