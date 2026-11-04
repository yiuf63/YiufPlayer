#include <algorithm>
#include <string>
#include <vector>
#include "activity/folder_picker_activity.hpp"
#include "utils/scanner.hpp"
#include <borealis/views/scrolling_frame.hpp>
#include "view/media_cell.hpp"
#include "ui/icons.hpp"
#include <borealis/views/dialog.hpp>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace deck_player {

FolderPickerActivity::FolderPickerActivity(OnPick on_pick)
    : on_pick_(std::move(on_pick)) {}

FolderPickerActivity::~FolderPickerActivity() = default;

brls::View* FolderPickerActivity::createContentView() {
    list_ = new brls::Box();
    list_->setAxis(brls::Axis::COLUMN);

    scroller_ = new brls::ScrollingFrame();
    scroller_->setGrow(1.0f);
    scroller_->setMarginRight(28.0f);
    styleScrollingFrame(scroller_);
    scroller_->setContentView(list_);

    frame_ = new brls::AppletFrame(scroller_);
    frame_->setTitle("选择文件夹");
    return frame_;
}

void FolderPickerActivity::onContentAvailable() {
    Activity::onContentAvailable();

    this->registerAction("返回", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (!history_.empty()) {
            navigateUp();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });

    render();
}

std::string FolderPickerActivity::currentTitle() const {
    if (current_path_.empty()) return "选择存储位置";
    std::string name = fs::path(current_path_).filename().string();
    if (name.empty()) name = current_path_;
    return name;
}

void FolderPickerActivity::navigateTo(const std::string& path) {
    if (!current_path_.empty()) history_.push_back(current_path_);
    current_path_ = path;
    render();
}

void FolderPickerActivity::navigateUp() {
    if (history_.empty()) {
        if (!current_path_.empty()) {
            current_path_.clear();
            render();
        }
        return;
    }
    current_path_ = history_.back();
    history_.pop_back();
    render();
}

void FolderPickerActivity::render() {
    if (!list_ || !frame_) return;
    frame_->setTitle(this->currentTitle());
    list_->clearViews();

    if (current_path_.empty()) {
        // 顶层：列出可用的存储位置
        list_->addView(new SectionHeader("选择要浏览的存储位置"));

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

        // 手动输入路径
        auto* manual = new MediaCell(ui::Icon::Plus, "手动输入路径",
                                     "适用于网络挂载点或未自动探测的目录");
        manual->setChevron(true);
        manual->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText(
                [this](std::string text) {
                    std::error_code ec;
                    if (text.empty()) return;
                    if (fs::exists(text, ec) && fs::is_directory(text, ec)) {
                        navigateTo(text);
                    } else {
                        brls::Application::notify("路径不存在或不是文件夹");
                    }
                },
                "输入文件夹绝对路径", "路径", 512);
            return true;
        });
        list_->addView(manual);
        return;
    }

    // 进入某目录
    auto* up = new MediaCell(ui::Icon::Back, "返回上一级", current_path_);
    up->setChevron(true);
    up->registerClickAction([this](brls::View*) {
        navigateUp();
        return true;
    });
    list_->addView(up);

    // 「选择当前文件夹」
    auto* pick_here = new MediaCell(ui::Icon::Check, "添加当前文件夹",
                                    "将整个 " + currentTitle() + " 加入媒体库");
    std::string here = current_path_;
    pick_here->registerClickAction([this, here](brls::View*) {
        if (on_pick_) on_pick_(here);
        brls::Application::popActivity();
        return true;
    });
    list_->addView(pick_here);

    list_->addView(new SectionHeader("子文件夹"));

    std::error_code ec;
    std::vector<std::string> dirs;
    fs::directory_iterator it(fs::path(current_path_),
        fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        for (const auto& entry : it) {
            std::error_code e2;
            if (!entry.is_directory(e2)) continue;
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            dirs.push_back(entry.path().string());
        }
    }
    std::sort(dirs.begin(), dirs.end());

    if (dirs.empty()) {
        list_->addView(new EmptyState(ui::Icon::Folder, "这里没有子文件夹",
                                      "可以直接用上面的「添加当前文件夹」"));
    } else {
        for (const auto& d : dirs) {
            auto* cell = new MediaCell(ui::Icon::Folder,
                                       fs::path(d).filename().string(), "文件夹");
            cell->setChevron(true);
            cell->registerClickAction([this, d](brls::View*) {
                navigateTo(d);
                return true;
            });
            list_->addView(cell);
        }
    }
    list_->setPaddingBottom(28.0f);
}

} // namespace deck_player
