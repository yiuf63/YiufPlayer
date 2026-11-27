#include "activity/series_activity.hpp"

#include <algorithm>
#include <vector>
#include <string>
#include <thread>
#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include "activity/player_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/js_plugin.hpp"
#include "ui/design.hpp"
#include "ui/icons.hpp"
#include "utils/i18n.hpp"
#include "utils/playback_history.hpp"
#include "utils/scanner.hpp"
#include "view/media_cell.hpp"
#include "view/now_playing_bar.hpp"
#include "view/tool_button.hpp"

namespace muvik {

SeriesActivity::SeriesActivity(SeriesGroup series)
    : series_(std::move(series)) {}

SeriesActivity::~SeriesActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* SeriesActivity::createContentView() {
    const auto& pal = ui::palette();

    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setGrow(1.0f);
    root->setPadding(16.0f, 28.0f, 16.0f, 28.0f);

    // ── 顶部 Hero 区域：海报/信息、简介、快捷续播按钮 ──
    auto* hero = new brls::Box();
    hero->setAxis(brls::Axis::COLUMN);
    hero->setWidthPercentage(100.0f);
    hero->setMarginBottom(16.0f);

    // 标题行（包含可见的返回按钮与作品标题）
    auto* title_box = new brls::Box();
    title_box->setAxis(brls::Axis::ROW);
    title_box->setAlignItems(brls::AlignItems::CENTER);
    title_box->setMarginBottom(6.0f);

    auto* back_btn = new ToolButton(i18n::tr("common.back", "返回"), ui::Icon::Back, 12.0f);
    back_btn->setMarginRight(14.0f);
    back_btn->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    title_box->addView(back_btn);

    std::string full_title = series_.title;
    if (!series_.release_date.empty()) {
        full_title += " (" + series_.release_date.substr(0, 4) + ")";
    }

    auto* title_lbl = new brls::Label();
    title_lbl->setText(full_title);
    title_lbl->setFontSize(ui::font::title());
    title_lbl->setTextColor(pal.text);
    title_box->addView(title_lbl);
    hero->addView(title_box);

    // 标签与评分：流派 · ★ 评分 · 共 X 集
    std::string tag_text;
    if (!series_.genres.empty()) tag_text += series_.genres + " · ";
    if (series_.rating > 0.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "★ %.1f · ", series_.rating);
        tag_text += buf;
    }
    tag_text += "共 " + std::to_string(series_.episodes.size()) + " 集";

    auto* tag_lbl = new brls::Label();
    tag_lbl->setText(tag_text);
    tag_lbl->setFontSize(ui::font::caption());
    tag_lbl->setTextColor(pal.accent);
    tag_lbl->setMarginBottom(8.0f);
    hero->addView(tag_lbl);

    // 剧集剧情概要
    if (!series_.overview.empty()) {
        auto* desc_lbl = new brls::Label();
        desc_lbl->setText(series_.overview);
        desc_lbl->setFontSize(ui::font::body());
        desc_lbl->setTextColor(pal.textDim);
        desc_lbl->setLineHeight(1.35f);
        desc_lbl->setMarginBottom(12.0f);
        hero->addView(desc_lbl);
    }

    // 主操作按钮：继续播放 / 从头播放
    std::string play_label;
    if (series_.last_watched_idx >= 0 && series_.last_watched_idx < (int)series_.episodes.size()) {
        // 用**真实季集号**，不是下标 +1。
        //
        // 实测（fixture: mixed-directory-with-missing-episodes）：
        // 目录里只有 S01E05 和 S01E08，上次看的是 E08。
        // E08 的下标是 1，写成「第 2 集」—— 用户会以为文件放错了。
        // 文件名里没有集号时（OP/ED、录屏）才退回下标。
        const int real_ep = series_.episodeOf(static_cast<size_t>(series_.last_watched_idx));
        const int shown_ep = (real_ep > 0) ? real_ep : (series_.last_watched_idx + 1);
        play_label = "继续播放：第 " + std::to_string(shown_ep) + " 集";
        if (series_.last_watched_pos > 0) {
            play_label += " (" + MediaScanner::formatDuration(series_.last_watched_pos) + ")";
        }
    } else {
        play_label = "从第 1 集开始播放";
    }

    play_btn_ = new ToolButton(play_label, ui::Icon::Play, 14.0f);
    play_btn_->setActive(true);
    play_btn_->registerClickAction([this](brls::View*) {
        int idx = (series_.last_watched_idx >= 0) ? series_.last_watched_idx : 0;
        playEpisode(static_cast<size_t>(idx));
        return true;
    });
    hero->addView(play_btn_);

    root->addView(hero);

    // ── 分隔线 ──
    auto* line = new brls::Box();
    line->setWidthPercentage(100.0f);
    line->setHeight(1.0f);
    line->setMarginBottom(12.0f);
    root->addView(line);

    // ── 选集列表区域 ──
    auto* scroller = new brls::ScrollingFrame();
    scroller->setGrow(1.0f);
    scroller->setWidthPercentage(100.0f);
    scroller->setMarginRight(20.0f);
    styleScrollingFrame(scroller);

    episode_list_ = new brls::Box();
    episode_list_->setAxis(brls::Axis::COLUMN);
    episode_list_->setWidthPercentage(100.0f);

    refreshEpisodeList();

    scroller->setContentView(episode_list_);
    root->addView(scroller);

    // 注册返回动作至容器节点
    root->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B,
                         [](brls::View*) {
                             brls::Application::popActivity();
                             return true;
                         });
    scroller->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B,
                            [](brls::View*) {
                                brls::Application::popActivity();
                                return true;
                            });

    frame_ = new brls::AppletFrame(root);
    frame_->setTitle(series_.title.empty() ? i18n::tr("media.series_detail", "剧集详情") : series_.title);
    frame_->setIcon(BRLS_ASSET("icon/icon.png"));

    NowPlayingBar::install(frame_);

    return frame_;
}

void SeriesActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 确保无论焦点在何处，手柄或快捷键按 B 均能安全退出当前剧集详情页面
    this->registerAction(i18n::tr("common.back", "返回"), brls::ControllerButton::BUTTON_B,
                         [](brls::View*) {
                             brls::Application::popActivity();
                             return true;
                         });

    if (play_btn_) {
        brls::Application::giveFocus(play_btn_);
    }
}

void SeriesActivity::refreshEpisodeList() {
    if (!episode_list_) return;
    episode_list_->clearViews();

    if (series_.episodes.empty()) {
        episode_list_->addView(new EmptyState(
            ui::Icon::Video, "暂无剧集分集",
            "未能检索到可播放的媒体分集文件"));
        return;
    }

    auto& hist = PlaybackHistory::getInstance();

    for (size_t i = 0; i < series_.episodes.size(); ++i) {
        const auto& item = series_.episodes[i];

        std::string detail = MediaScanner::formatFileSize(item.size);
        if (!item.matched_subtitles.empty()) {
            detail += " · 含字幕";
        }

        auto* cell = new MediaCell(ui::Icon::Video, item.name, detail);
        cell->setHeight(64.0f);

        HistoryRecord rec;
        if (hist.getRecord(item.path, rec)) {
            if (rec.completed) {
                cell->setBadge(i18n::tr("common.played", "已看完"));
                cell->setProgress(1.0f);
            } else if (rec.duration > 0 && rec.position > 10) {
                float ratio = static_cast<float>((double)rec.position / rec.duration);
                cell->setProgress(ratio);
                std::string badge_str = "看到 " + MediaScanner::formatDuration(rec.position);
                if (static_cast<int>(i) == series_.last_watched_idx) {
                    badge_str += " · 续播";
                }
                cell->setBadge(badge_str);
            }
        }

        cell->registerClickAction([this, i](brls::View*) {
            playEpisode(i);
            return true;
        });

        episode_list_->addView(cell);
    }
}

void SeriesActivity::playEpisode(size_t index) {
    if (index >= series_.episodes.size()) return;

    // 检查是否为流媒体插件剧集（Kazumi 模型），调用 loadLinks 解析真实播放直链与 Headers
    auto js_plug = PluginManager::getInstance().getJSPlugin(series_.folder_path);
    if (js_plug && js_plug->hasMediaStream()) {
        const auto& ep = series_.episodes[index];
        brls::Application::notify("正在解析剧集播放直链…");
        std::string folder_path = series_.folder_path;
        auto episodes = series_.episodes;
        std::string ep_path = ep.path;
        std::thread([folder_path, episodes, index, ep_path]() {
            PlayLinksResult links;
            bool ok = PluginManager::getInstance().loadStreamLinks(folder_path, ep_path, links);
            brls::sync([episodes, index, ok, links]() {
                if (ok && !links.streams.empty()) {
                    std::vector<MediaItem> resolved_episodes = episodes;
                    resolved_episodes[index].path = links.streams[0].url;
                    auto* player = new PlayerActivity(resolved_episodes, index);
                    if (!links.streams[0].headers.empty()) {
                        player->setStreamHeaders(links.streams[0].headers);
                    }
                    brls::Application::pushActivity(player);
                } else {
                    brls::Application::pushActivity(new PlayerActivity(episodes, index));
                }
            });
        }).detach();
        return;
    }

    brls::Application::pushActivity(new PlayerActivity(series_.episodes, index));
}

} // namespace muvik
