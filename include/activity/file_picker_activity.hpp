#pragma once

#include <functional>
#include <string>
#include <vector>

#include <borealis.hpp>

namespace deck_player {

// 通用「选目录 / 选文件」界面。
//
// 为什么需要它：掌机与电视盒上没有系统文件管理器，任何「挑一个文件」的需求
// 都得自己实现。原来只有 FolderPickerActivity（只列目录），于是「加载外挂字幕」
// 退化成了 IME 输入绝对路径 —— 让用户在 Steam Deck 上敲
// /run/media/mmcblk0p1/Videos/Movie.2024.2160p/movie.chs.ass，实际不可用。
//
// Folder 模式：存储设备 → 目录 → 「添加当前文件夹」
// File   模式：存储设备 → 目录 → 只列匹配扩展名的文件；
//              与 prefer_prefix 匹配的排在最前并标「同名」
class FilePickerActivity : public brls::Activity {
public:
    using OnPick = std::function<void(const std::string& path)>;

    // Folder 模式（添加媒体文件夹）
    static FilePickerActivity* folders(OnPick on_pick);

    // File 模式（加载外挂字幕等）
    //   extensions    小写扩展名，含点，如 {".srt", ".ass"}；空表示不过滤
    //   start_dir     初始目录，空则从存储设备列表开始
    //   prefer_prefix 期望匹配的文件名前缀（通常是当前视频的片名），可空
    static FilePickerActivity* files(std::vector<std::string> extensions,
                                     std::string start_dir,
                                     std::string prefer_prefix,
                                     std::string title,
                                     OnPick on_pick);

    ~FilePickerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    enum class Mode { Folder, File };

    FilePickerActivity() = default;

    void render();
    void renderRoot();
    void renderDirectory();
    void navigateTo(const std::string& path);
    void navigateUp();
    std::string currentTitle() const;
    bool isStorageRoot(const std::string& path) const;

    bool isFileMode() const { return mode_ == Mode::File; }

    Mode mode_ = Mode::Folder;
    std::vector<std::string> extensions_;
    std::string prefer_prefix_;
    std::string title_;

    OnPick on_pick_;
    std::string current_path_;
    std::vector<std::string> history_;

    brls::AppletFrame* frame_ = nullptr;
    brls::ScrollingFrame* scroller_ = nullptr;
    brls::Box* list_ = nullptr;
};

} // namespace deck_player
