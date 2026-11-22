#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "activity/plugin_browser_activity.hpp"
#include "activity/player_activity.hpp"
#include "activity/series_activity.hpp"
#include "plugin/plugin_manager.hpp"
#include "plugin/js_plugin.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include <borealis/views/dialog.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <cstdlib>

namespace muvik {

PluginBrowserActivity::PluginBrowserActivity(std::string plugin_path,
                                             std::string plugin_name)
    : plugin_path_(std::move(plugin_path)),
      plugin_name_(std::move(plugin_name)) {}

PluginBrowserActivity::~PluginBrowserActivity() {
    for (auto& f : alive_flags_) {
        if (f) f->store(false);
    }
    alive_flags_.clear();
}

brls::View* PluginBrowserActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setMarginRight(28.0f);
    styleScrollingFrame(scroller_);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle(plugin_name_);
    return frame_;
}

void PluginBrowserActivity::onContentAvailable() {
    Activity::onContentAvailable();

    // 深链/测试：从指定路径开始浏览
    if (!initial_path_.empty()) {
        current_path_ = initial_path_;
        initial_path_.clear();
    }

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (is_searching_) {
            clearSearch();
            return true;
        }
        if (!history_.empty()) {
            navigateUp();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });

    // 搜索（网盘文件或番剧内容）
    this->registerAction("搜索", brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        promptSearch();
        return true;
    });

    // 刷新当前目录
    this->registerAction("刷新", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        if (is_searching_) {
            performSearch(search_query_);
        } else {
            render();
        }
        return true;
    });

    render();
}

std::string PluginBrowserActivity::currentTitle() const {
    if (is_searching_) {
        return plugin_name_ + " / 搜索: " + search_query_;
    }
    std::string base = plugin_name_;
    if (current_path_.empty() || current_path_ == "/") return base;
    // 取路径最后一段
    std::string p = current_path_;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    std::string seg = (slash == std::string::npos) ? p : p.substr(slash + 1);
    return seg.empty() ? base : base + " / " + seg;
}

void PluginBrowserActivity::navigateTo(const std::string& path) {
    if (!current_path_.empty()) history_.push_back(current_path_);
    current_path_ = path;
    render();
}

void PluginBrowserActivity::navigateUp() {
    // 若当前在子目录，返回上一级
    if (current_path_.empty() || current_path_ == "/") {
        brls::Application::popActivity();
        return;
    }
    // 优先用历史记录（保持用户实际浏览路径）
    if (!history_.empty()) {
        current_path_ = history_.back();
        history_.pop_back();
        render();
        return;
    }
    // 无历史时按路径截断
    std::string p = current_path_;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        current_path_ = initial_path_.empty() ? "/" : initial_path_;
    } else {
        current_path_ = p.substr(0, slash);
    }
    render();
}

void PluginBrowserActivity::render() {
    if (!list_ || !frame_) return;

    frame_->setTitle(currentTitle());
    list_->clearViews();
    loading_ = true;

    // 加载提示
    list_->addView(new EmptyState(ui::Icon::Plugin, "正在从插件读取目录…",
                                  "网络较慢时可能需要几秒"));

    // 网络请求放到后台，避免阻塞 UI
    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    std::string path = current_path_;
    std::string plugin = plugin_path_;

    std::thread([this, alive, path, plugin]() {
        std::vector<MediaItem> items;
        bool ok = PluginManager::getInstance().listVideoSourceItems(plugin, path, items);

        brls::sync([this, alive, ok, items, path, plugin]() {
            if (!alive->load()) return;   // 活动已销毁
            if (!list_ || !frame_) return;

            loading_ = false;
            list_->clearViews();

            if (!ok) {
                list_->addView(new EmptyState(
                    ui::Icon::Error, "无法读取该目录",
                    "首次使用请先在「插件管理」里配置服务器地址\n也可以按 Y 重试"));
                return;
            }

            // 返回上一级
            if (!path.empty() && path != "/") {
                auto* up = new MediaCell(ui::Icon::Back, "..");
                up->setChevron(true);
                up->registerClickAction([this](brls::View*) {
                    navigateUp();
                    return true;
                });
                list_->addView(up);
            }

            // 搜索条目（支持快速输入检索）
            auto* search_bar = new MediaCell(ui::Icon::Search, "搜索此源的内容…", "按 X 键或点击输入关键词检索");
            search_bar->setChevron(true);
            search_bar->registerClickAction([this](brls::View*) {
                promptSearch();
                return true;
            });
            list_->addView(search_bar);

            // 分目录与可播放条目
            std::vector<MediaItem> dirs, playable;
            for (const auto& it : items) {
                // 注意：插件可能返回 Unknown 类型，需显式判断
                if (it.is_directory()) dirs.push_back(it);
                else if (it.is_video() || it.is_audio()) playable.push_back(it);
            }

            if (dirs.empty() && playable.empty()) {
                list_->addView(new EmptyState(ui::Icon::Folder, "这个目录是空的",
                                              "没有找到可播放的媒体"));
                return;
            }

            if (!dirs.empty()) {
                list_->addView(new SectionHeader(
                    "文件夹 · " + std::to_string(dirs.size())));
                for (const auto& d : dirs) {
                    auto* cell = new MediaCell(ui::Icon::Folder, d.name, "文件夹");
                    cell->setChevron(true);
                    std::string target = d.path;
                    std::string current_plugin = plugin;
                    std::string d_name = d.name;
                    cell->registerClickAction([this, target, current_plugin, d_name](brls::View*) {
                        // 检查是否为流媒体剧集详情 (Kazumi 模型)
                        MediaDetail detail;
                        if (PluginManager::getInstance().loadDetail(current_plugin, target, detail) && !detail.episodes.empty()) {
                            SeriesGroup sg;
                            sg.id = detail.id.empty() ? target : detail.id;
                            sg.title = detail.title.empty() ? d_name : detail.title;
                            sg.overview = detail.overview;
                            sg.poster_url = detail.poster_url;
                            sg.backdrop_url = detail.backdrop_url;
                            sg.release_date = detail.release_date;
                            sg.folder_path = current_plugin; // 借用记录插件路径以解析分集
                            for (const auto& ep : detail.episodes) {
                                MediaItem mi;
                                mi.name = ep.title;
                                mi.path = ep.id;
                                mi.type = MediaType::Video;
                                sg.episodes.push_back(mi);
                            }
                            brls::Application::pushActivity(new SeriesActivity(sg));
                            return true;
                        }
                        navigateTo(target);
                        return true;
                    });
                    list_->addView(cell);
                }
            }

            if (!playable.empty()) {
                list_->addView(new SectionHeader(
                    "媒体 · " + std::to_string(playable.size())));
                for (size_t i = 0; i < playable.size(); ++i) {
                    const bool is_video = playable[i].is_video();
                    auto* cell = new MediaCell(
                        is_video ? ui::Icon::Video : ui::Icon::Music,
                        playable[i].name,
                        is_video ? "视频" : "音频");
                    size_t idx = i;
                    std::vector<MediaItem> playlist = playable;
                    std::string current_plugin = plugin;
                    cell->registerClickAction([playlist, idx, current_plugin](brls::View*) {
                        const auto& target_item = playlist[idx];
                        // 1. 直链或本地文件直接播放
                        if (target_item.path.rfind("http://", 0) == 0 ||
                            target_item.path.rfind("https://", 0) == 0 ||
                            target_item.path.rfind("rtmp://", 0) == 0 ||
                            target_item.path.rfind("rtsp://", 0) == 0) {
                            brls::Application::pushActivity(new PlayerActivity(playlist, idx));
                            return true;
                        }

                        // 2. 动态预签名直链解析 (PikPak / 移动云盘等网盘)
                        brls::Application::notify("正在解析播放直链…");
                        std::thread([playlist, idx, current_plugin, target_item]() {
                            StreamSource resolved_source;
                            bool ok = PluginManager::getInstance().resolvePlayUrl(current_plugin, target_item.path, resolved_source);
                            brls::sync([playlist, idx, ok, resolved_source]() {
                                std::vector<MediaItem> resolved_list = playlist;
                                if (ok && !resolved_source.url.empty()) {
                                    resolved_list[idx].path = resolved_source.url;
                                    auto* player_act = new PlayerActivity(resolved_list, idx);
                                    if (!resolved_source.headers.empty()) {
                                        player_act->setStreamHeaders(resolved_source.headers);
                                    }
                                    brls::Application::pushActivity(player_act);
                                } else {
                                    brls::Application::pushActivity(new PlayerActivity(playlist, idx));
                                }
                            });
                        }).detach();
                        return true;
                    });
                    list_->addView(cell);
                }

                if (std::getenv("MUVIK_AUTOPLAY_FIRST_VIDEO")) {
                    brls::Logger::info("测试钩子：自动触发首个视频播放: {}", playable[0].name);
                    const auto& target_item = playable[0];
                    std::string current_plugin = plugin;
                    std::vector<MediaItem> playlist = playable;
                    std::thread([playlist, current_plugin, target_item]() {
                        StreamSource resolved_source;
                        bool ok = PluginManager::getInstance().resolvePlayUrl(current_plugin, target_item.path, resolved_source);
                        brls::sync([playlist, ok, resolved_source]() {
                            std::vector<MediaItem> resolved_list = playlist;
                            if (ok && !resolved_source.url.empty()) {
                                resolved_list[0].path = resolved_source.url;
                                auto* player_act = new PlayerActivity(resolved_list, 0);
                                if (!resolved_source.headers.empty()) {
                                    player_act->setStreamHeaders(resolved_source.headers);
                                }
                                brls::Application::pushActivity(player_act);
                            } else {
                                brls::Application::pushActivity(new PlayerActivity(playlist, 0));
                            }
                        });
                    }).detach();
                }
            }
            list_->setPaddingBottom(28.0f);
        });
    }).detach();
}

void PluginBrowserActivity::promptSearch() {
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(0, 1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();

            if (text.empty()) {
                clearSearch();
            } else {
                search_query_ = text;
                is_searching_ = true;
                performSearch(search_query_);
            }
        },
        "搜索 - " + plugin_name_,
        "输入关键词进行搜索，留空清除",
        64);
}

void PluginBrowserActivity::clearSearch() {
    is_searching_ = false;
    search_query_.clear();
    render();
}

void PluginBrowserActivity::performSearch(const std::string& query) {
    if (!list_ || !frame_) return;

    frame_->setTitle(plugin_name_ + " / 搜索: " + query);
    list_->clearViews();
    loading_ = true;

    auto* clear_cell = new MediaCell(ui::Icon::Back, "退出搜索结果 (" + query + ")", "点击或按 B 返回目录");
    clear_cell->registerClickAction([this](brls::View*) {
        clearSearch();
        return true;
    });
    list_->addView(clear_cell);

    list_->addView(new EmptyState(ui::Icon::Search, "正在搜索 \"" + query + "\"…",
                                  "正在查询匹配的内容"));

    auto alive = std::make_shared<std::atomic<bool>>(true);
    alive_flags_.push_back(alive);

    std::string plugin = plugin_path_;
    std::string current_p = current_path_;

    std::thread([this, alive, plugin, query, current_p]() {
        std::vector<MediaItem> results;
        bool ok = false;
        bool has_more = false;

        if (PluginManager::getInstance().canSearch(plugin)) {
            ok = PluginManager::getInstance().searchVideoSource(plugin, query, 1, results, has_more, current_p);
        } else {
            // 本地模糊过滤
            std::vector<MediaItem> all_items;
            if (PluginManager::getInstance().listVideoSourceItems(plugin, current_p, all_items)) {
                std::string lq = query;
                std::transform(lq.begin(), lq.end(), lq.begin(), ::tolower);
                for (const auto& it : all_items) {
                    std::string ln = it.name;
                    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
                    if (ln.find(lq) != std::string::npos) {
                        results.push_back(it);
                    }
                }
                ok = true;
            }
        }

        brls::sync([this, alive, ok, results, query]() {
            if (!alive->load()) return;
            if (!list_ || !frame_) return;
            loading_ = false;
            renderSearchResults(query, ok, results);
        });
    }).detach();
}

void PluginBrowserActivity::renderSearchResults(const std::string& query, bool ok, const std::vector<MediaItem>& results) {
    list_->clearViews();

    auto* clear_cell = new MediaCell(ui::Icon::Back, "退出搜索结果 (" + query + ")", "点击或按 B 返回目录");
    clear_cell->registerClickAction([this](brls::View*) {
        clearSearch();
        return true;
    });
    list_->addView(clear_cell);

    if (!ok) {
        list_->addView(new EmptyState(ui::Icon::Error, "搜索失败", "请检查网络或配置，按 Y 重新搜索"));
        return;
    }

    if (results.empty()) {
        list_->addView(new EmptyState(ui::Icon::Search, "未找到与 \"" + query + "\" 相关的结果",
                                      "请尝试更换关键词，或点击上方退出搜索"));
        return;
    }

    std::vector<MediaItem> dirs, playable;
    for (const auto& it : results) {
        if (it.is_directory()) dirs.push_back(it);
        else if (it.is_video() || it.is_audio()) playable.push_back(it);
    }

    if (!dirs.empty()) {
        list_->addView(new SectionHeader("文件夹 · " + std::to_string(dirs.size())));
        for (const auto& d : dirs) {
            auto* cell = new MediaCell(ui::Icon::Folder, d.name, "文件夹");
            cell->setChevron(true);
            std::string target = d.path;
            std::string current_plugin = plugin_path_;
            std::string d_name = d.name;
            cell->registerClickAction([this, target, current_plugin, d_name](brls::View*) {
                // 检查是否为流媒体剧集详情 (Kazumi 模型)
                MediaDetail detail;
                if (PluginManager::getInstance().loadDetail(current_plugin, target, detail) && !detail.episodes.empty()) {
                    SeriesGroup sg;
                    sg.id = detail.id.empty() ? target : detail.id;
                    sg.title = detail.title.empty() ? d_name : detail.title;
                    sg.overview = detail.overview;
                    sg.poster_url = detail.poster_url;
                    sg.backdrop_url = detail.backdrop_url;
                    sg.release_date = detail.release_date;
                    sg.folder_path = current_plugin;
                    for (const auto& ep : detail.episodes) {
                        MediaItem mi;
                        mi.name = ep.title;
                        mi.path = ep.id;
                        mi.type = MediaType::Video;
                        sg.episodes.push_back(mi);
                    }
                    brls::Application::pushActivity(new SeriesActivity(sg));
                    return true;
                }
                clearSearch();
                navigateTo(target);
                return true;
            });
            list_->addView(cell);
        }
    }

    if (!playable.empty()) {
        list_->addView(new SectionHeader("搜索结果 · " + std::to_string(playable.size())));
        for (size_t i = 0; i < playable.size(); ++i) {
            const bool is_video = playable[i].is_video();
            auto* cell = new MediaCell(
                is_video ? ui::Icon::Video : ui::Icon::Music,
                playable[i].name,
                is_video ? "视频" : "音频");
            size_t idx = i;
            std::vector<MediaItem> playlist = playable;
            std::string current_plugin = plugin_path_;
            cell->registerClickAction([playlist, idx, current_plugin](brls::View*) {
                const auto& target_item = playlist[idx];
                if (target_item.path.rfind("http://", 0) == 0 ||
                    target_item.path.rfind("https://", 0) == 0 ||
                    target_item.path.rfind("rtmp://", 0) == 0 ||
                    target_item.path.rfind("rtsp://", 0) == 0) {
                    brls::Application::pushActivity(new PlayerActivity(playlist, idx));
                    return true;
                }

                brls::Application::notify("正在解析播放直链…");
                std::thread([playlist, idx, current_plugin, target_item]() {
                    StreamSource resolved_source;
                    bool ok = PluginManager::getInstance().resolvePlayUrl(current_plugin, target_item.path, resolved_source);
                    brls::sync([playlist, idx, ok, resolved_source]() {
                        std::vector<MediaItem> resolved_list = playlist;
                        if (ok && !resolved_source.url.empty()) {
                            resolved_list[idx].path = resolved_source.url;
                            auto* player_act = new PlayerActivity(resolved_list, idx);
                            if (!resolved_source.headers.empty()) {
                                player_act->setStreamHeaders(resolved_source.headers);
                            }
                            brls::Application::pushActivity(player_act);
                        } else {
                            brls::Application::pushActivity(new PlayerActivity(playlist, idx));
                        }
                    });
                }).detach();
                return true;
            });
            list_->addView(cell);
        }
    }
    list_->setPaddingBottom(28.0f);
}

} // namespace muvik
