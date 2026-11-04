#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "activity/file_picker_activity.hpp"
#include "utils/scanner.hpp"
#include "view/media_cell.hpp"
#include "ui/icons.hpp"

#include <borealis/views/scrolling_frame.hpp>

namespace fs = std::filesystem;

namespace deck_player {

namespace {

constexpr float kContentSafeMargin = 28.0f;

const char* kDefaultTitle = "选择文件夹";

std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string extOf(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return {};
    return lowerAscii(name.substr(dot));
}

// 「与片名匹配」的判定。用户希望的是「打开这集的外挂字幕」，
// 所以同名前缀最优先；片名里含字幕文件的词干（例如简体中文.srt）也算匹配。
bool matchesPrefix(const std::string& filename, const std::string& prefix) {
    if (prefix.empty()) return false;
    const std::string stem = fs::path(filename).stem().string();
    const std::string a = lowerAscii(stem);
    const std::string b = lowerAscii(prefix);
    if (a.rfind(b, 0) == 0) return true;   // 文件名以片名开头
    if (b.rfind(a, 0) == 0) return true;   // 片名以文件名开头
    return a.find(b) != std::string::npos;
}

} // namespace

FilePickerActivity* FilePickerActivity::folders(OnPick on_pick) {
    auto* a = new FilePickerActivity();
    a->mode_ = Mode::Folder;
    a->title_ = kDefaultTitle;
    a->on_pick_ = std::move(on_pick);
    return a;
}

FilePickerActivity* FilePickerActivity::files(std::vector<std::string> extensions,
                                              std::string start_dir,
                                              std::string prefer_prefix,
                                              std::string title,
                                              OnPick on_pick) {
    auto* a = new FilePickerActivity();
    a->mode_ = Mode::File;
    a->extensions_ = std::move(extensions);
    for (auto& e : a->extensions_) e = lowerAscii(e);
    a->prefer_prefix_ = std::move(prefer_prefix);
    a->title_ = title.empty() ? "选择文件" : std::move(title);
    a->on_pick_ = std::move(on_pick);

    // 默认停在起始目录：同名字幕 90% 的情况已经在那里了，
    // 让用户从 / 开始翻是把这个便利白白扔掉。
    if (!start_dir.empty()) {
        std::error_code ec;
        if (fs::is_directory(start_dir, ec)) a->current_path_ = start_dir;
    }
    return a;
}

FilePickerActivity::~FilePickerActivity() = default;

brls::View* FilePickerActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);
    list_->setPaddingBottom(kContentSafeMargin);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setMarginRight(kContentSafeMargin);
    styleScrollingFrame(scroller_);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle(title_);
    return frame_;
}

void FilePickerActivity::onContentAvailable() {
    Activity::onContentAvailable();

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        // 在目录里先回上一级，到顶层才退出
        if (!current_path_.empty() || !history_.empty()) {
            navigateUp();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });

    render();
}

std::string FilePickerActivity::currentTitle() const {
    if (current_path_.empty()) {
        return isFileMode() ? title_ : "选择存储位置";
    }
    std::string name = fs::path(current_path_).filename().string();
    if (name.empty()) name = current_path_;
    return name;
}

bool FilePickerActivity::isStorageRoot(const std::string& path) const {
    for (const auto& src : MediaScanner::getInstance().getAvailableSources()) {
        if (src.path == path) return true;
    }
    return false;
}

void FilePickerActivity::navigateTo(const std::string& path) {
    if (!current_path_.empty()) history_.push_back(current_path_);
    current_path_ = path;
    render();
}

void FilePickerActivity::navigateUp() {
    if (!history_.empty()) {
        current_path_ = history_.back();
        history_.pop_back();
        render();
        return;
    }
    if (current_path_.empty()) return;

    // 没有历史（例如从 start_dir 直接进来）：
    // 能往上就往上，到了存储设备根或 / 就回顶层列表。
    const fs::path p(current_path_);
    const fs::path parent = p.parent_path();
    if (!parent.empty() && parent != p && parent != fs::path("/") &&
        !isStorageRoot(current_path_)) {
        current_path_ = parent.string();
    } else {
        current_path_.clear();
    }
    render();
}

void FilePickerActivity::render() {
    if (!list_ || !frame_) return;
    frame_->setTitle(currentTitle());
    list_->clearViews();

    if (current_path_.empty()) {
        renderRoot();
    } else {
        renderDirectory();
    }
}

void FilePickerActivity::renderRoot() {
    list_->addView(new SectionHeader(isFileMode() ? "从哪个存储位置找" : "选择要浏览的存储位置"));

    auto sources = MediaScanner::getInstance().getAvailableSources();
    for (const auto& src : sources) {
        auto* cell = new MediaCell(ui::Icon::Drive, src.name, src.path);
        cell->setChevron(true);
        std::string target = src.path;
        cell->registerClickAction([this, target](brls::View*) {
            navigateTo(target);
            return true;
        });
        list_->addView(cell);
    }

    if (sources.empty()) {
        list_->addView(new EmptyState(ui::Icon::Drive, "没有找到可用的存储设备",
                                      "可以用下面的手动输入作为兜底"));
    }

    // 兜底：手动输入。放在最后，不再是唯一入口。
    auto* manual = new MediaCell(
        ui::Icon::Plus, isFileMode() ? "手动输入文件路径" : "手动输入路径",
        "适用于网络挂载点或未自动探测的目录");
    manual->setChevron(true);
    manual->registerClickAction([this](brls::View*) {
        const bool file_mode = isFileMode();
        brls::Application::getImeManager()->openForText(
            [this, file_mode](std::string text) {
                std::error_code ec;
                if (text.empty()) return;
                if (fs::is_directory(text, ec)) {
                    navigateTo(text);
                    return;
                }
                if (file_mode && fs::is_regular_file(text, ec)) {
                    if (on_pick_) on_pick_(text);
                    brls::Application::popActivity();
                    return;
                }
                brls::Application::notify("路径不存在");
            },
            isFileMode() ? "输入文件绝对路径" : "输入文件夹绝对路径", "路径", 512);
        return true;
    });
    list_->addView(manual);
}

void FilePickerActivity::renderDirectory() {
    // 返回上一级
    auto* up = new MediaCell(ui::Icon::Back, "返回上一级", current_path_);
    up->setChevron(true);
    up->registerClickAction([this](brls::View*) {
        navigateUp();
        return true;
    });
    list_->addView(up);

    // Folder 模式才有「选择当前文件夹」
    if (!isFileMode()) {
        auto* pick_here = new MediaCell(ui::Icon::Check, "添加当前文件夹",
                                        "将整个 " + currentTitle() + " 加入媒体库");
        std::string here = current_path_;
        pick_here->registerClickAction([this, here](brls::View*) {
            if (on_pick_) on_pick_(here);
            brls::Application::popActivity();
            return true;
        });
        list_->addView(pick_here);
    }

    // 扫目录：文件与子目录各收一份
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    std::error_code ec;
    fs::directory_iterator it(fs::path(current_path_),
                              fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code e2;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;

            if (entry.is_directory(e2)) {
                dirs.push_back(entry.path().string());
                continue;
            }
            if (!isFileMode() || !entry.is_regular_file(e2)) continue;

            if (!extensions_.empty()) {
                const std::string ext = extOf(name);
                if (std::find(extensions_.begin(), extensions_.end(), ext) == extensions_.end()) {
                    continue;
                }
            }
            files.push_back(entry.path().string());
        }
    }
    std::sort(dirs.begin(), dirs.end());

    // 匹配片名的排最前，其余按文件名
    std::stable_sort(files.begin(), files.end(),
                     [this](const std::string& a, const std::string& b) {
                         const bool ma = matchesPrefix(fs::path(a).filename().string(), prefer_prefix_);
                         const bool mb = matchesPrefix(fs::path(b).filename().string(), prefer_prefix_);
                         if (ma != mb) return ma;
                         return a < b;
                     });

    // 子目录
    if (!dirs.empty()) {
        list_->addView(new SectionHeader("子文件夹"));
        for (const auto& d : dirs) {
            auto* cell = new MediaCell(ui::Icon::Folder, fs::path(d).filename().string(), "文件夹");
            cell->setChevron(true);
            cell->registerClickAction([this, d](brls::View*) {
                navigateTo(d);
                return true;
            });
            list_->addView(cell);
        }
    }

    if (isFileMode()) {
        const std::string header = files.empty()
            ? "字幕文件"
            : "字幕文件 · " + std::to_string(files.size());
        list_->addView(new SectionHeader(header));

        if (files.empty()) {
            std::string hint = "这个文件夹里没有可用的字幕文件";
            if (!extensions_.empty()) {
                hint += "\n支持 ";
                for (size_t i = 0; i < extensions_.size(); ++i) {
                    if (i) hint += " / ";
                    hint += extensions_[i];
                }
            }
            list_->addView(new EmptyState(ui::Icon::Subtitles, "没有找到字幕文件", hint));
        } else {
            for (const auto& f : files) {
                const std::string name = fs::path(f).filename().string();
                const bool matched = matchesPrefix(name, prefer_prefix_);
                auto* cell = new MediaCell(ui::Icon::Subtitles, name,
                                           matched ? "与当前片名匹配" : "字幕文件");
                if (matched) cell->setBadge("同名");
                cell->registerClickAction([this, f](brls::View*) {
                    if (on_pick_) on_pick_(f);
                    brls::Application::popActivity();
                    return true;
                });
                list_->addView(cell);
            }
        }
        return;
    }

    // Folder 模式：只有子目录
    if (dirs.empty()) {
        list_->addView(new EmptyState(ui::Icon::Folder, "这里没有子文件夹",
                                      "可以直接用上面的「添加当前文件夹」"));
    }
}

} // namespace deck_player
