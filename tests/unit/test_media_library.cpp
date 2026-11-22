// 单元测试：媒体库（文件夹管理、递归扫描、过滤、持久化）
// 用临时目录构造真实文件树，走真实扫描逻辑。

#include "model/media_library.hpp"
#include "utils/app_paths.hpp"
#include "test_framework.hpp"
#include <cstdlib>
#include <fstream>
#include <filesystem>

using namespace muvik;
namespace fs = std::filesystem;

namespace {

void writeFile(const std::string& path) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path);
    out << "dummy";
}

} // namespace

int main() {
    TEST_SUITE();
    setenv("XDG_CONFIG_HOME", "/tmp/muvik_test_library", 1);
    std::system("rm -rf /tmp/muvik_test_library /tmp/muvik_test_media");

    // 构造测试文件树
    const std::string root = "/tmp/muvik_test_media";
    writeFile(root + "/media/a.mp4");
    writeFile(root + "/media/b.mkv");
    writeFile(root + "/media/anim.swf");          // SWF 格式支持
    writeFile(root + "/media/sub/c.avi");
    writeFile(root + "/media/sub/subsub/d.webm");
    writeFile(root + "/media/readme.txt");        // 非媒体，应被过滤
    writeFile(root + "/media/.hidden.mp4");       // 隐藏文件，应被过滤
    writeFile(root + "/media/a.srt");             // 外挂字幕
    writeFile(root + "/music/song1.flac");
    writeFile(root + "/music/song2.mp3");

    auto& lib = MediaLibrary::getInstance();

    TEST_BEGIN("媒体库 / 文件夹管理");
    CHECK_TRUE(lib.addFolder(root + "/media", "影视"), "添加 media 文件夹");
    CHECK_TRUE(lib.addFolder(root + "/music", "音乐"), "添加 music 文件夹");
    CHECK_FALSE(lib.addFolder(root + "/media"), "重复添加被拒绝");
    CHECK_FALSE(lib.addFolder("/tmp/definitely_not_exist_xyz"), "不存在的路径被拒绝");
    CHECK_EQ(lib.getFolders().size(), (size_t)2, "文件夹数量为 2");
    CHECK_TRUE(lib.hasFolder(root + "/media"), "hasFolder 正确");

    TEST_BEGIN("媒体库 / 递归扫描");
    lib.rebuild();
    auto videos = lib.getVideos();
    auto audios = lib.getAudios();
    CHECK_EQ(videos.size(), (size_t)5, "递归找到 5 个视频（含 SWF 与两层子目录）");
    CHECK_EQ(audios.size(), (size_t)2, "找到 2 个音频");

    bool has_swf = false;
    for (const auto& v : videos) {
        if (v.name == "anim.swf") has_swf = true;
    }
    CHECK_TRUE(has_swf, "SWF 文件成功作为视频媒体被索引");

    TEST_BEGIN("媒体库 / 过滤规则");
    bool has_txt = false, has_hidden = false, has_srt = false;
    for (const auto& item : lib.getItems()) {
        if (item.name.find("readme") != std::string::npos) has_txt = true;
        if (item.name.find(".hidden") != std::string::npos) has_hidden = true;
        if (item.name.find(".srt") != std::string::npos) has_srt = true;
    }
    CHECK_FALSE(has_txt, "非媒体文件被过滤");
    CHECK_FALSE(has_hidden, "隐藏文件被过滤");
    CHECK_FALSE(has_srt, "字幕文件不作为播放条目");

    TEST_BEGIN("媒体库 / 外挂字幕匹配");
    {
        bool matched = false;
        for (const auto& v : videos) {
            if (v.name == "a.mp4") matched = !v.matched_subtitles.empty();
        }
        CHECK_TRUE(matched, "a.mp4 匹配到同名外挂字幕 a.srt");
    }

    TEST_BEGIN("媒体库 / 持久化");
    lib.save();
    CHECK_TRUE(fs::exists(AppPaths::configFile("libraries.dat")) ||
               fs::exists("/tmp/muvik_test_library/yiufplayer/libraries.dat") ||
               fs::exists("/tmp/muvik_test_library/muvik/libraries.dat"),
               "libraries.dat 已生成");
    CHECK_TRUE(fs::exists(AppPaths::configFile("library_cache.dat")) ||
               fs::exists("/tmp/muvik_test_library/yiufplayer/library_cache.dat") ||
               fs::exists("/tmp/muvik_test_library/muvik/library_cache.dat"),
               "library_cache.dat 已生成");

    TEST_BEGIN("媒体库 / 移除文件夹");
    CHECK_TRUE(lib.removeFolder(root + "/media"), "移除 media");
    CHECK_EQ(lib.getVideos().size(), (size_t)0, "移除后视频清零");
    CHECK_EQ(lib.getAudios().size(), (size_t)2, "音乐不受影响");
    CHECK_FALSE(lib.removeFolder("/tmp/not_in_library"), "移除不存在的文件夹返回 false");

    TEST_END();
    std::system("rm -rf /tmp/muvik_test_media");
    return TEST_SUMMARY();
}
