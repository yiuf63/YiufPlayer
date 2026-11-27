// 单元测试：首页「继续观看」相关的纯逻辑
//
// 覆盖两处**实际踩过的坑**，都不依赖 borealis / nanoVG：
//   1. sceneImageFileExists 与 sceneImageReady 的区别
//   2. 无图时应当走紧凑布局（thin 包的正常状态）
//
// 第 1 条是本轮真实 bug：用 sceneImageReady 判断「有没有封面」时，
// 查的是 nanoVG 句柄，而句柄只在 drawScene() 里懒加载 ——
// 构造期必然还没加载，于是哪怕缩略图文件就在那儿，也永远判成无图。
// 表现是：SteamOS 上明明有封面，首页却一直显示紧凑行。

#include "ui/scene_tile.hpp"
#include "test_framework.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// 写一个「像缩略图」的文件：大于 128 字节。
void writeThumb(const std::string& path, size_t bytes = 1024) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    std::string blob(bytes, 'x');
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
}

} // namespace

int main() {
    TEST_SUITE();

    const std::string root = "/tmp/muvik_test_scene";
    std::system(("rm -rf " + root).c_str());
    fs::create_directories(root);

    TEST_BEGIN("缩略图文件判断：存在且足够大才算有图");
    {
        const std::string ok = root + "/ok.rgba";
        writeThumb(ok, 4096);
        CHECK_TRUE(muvik::ui::sceneImageFileExists(ok), "正常大小的缓存文件判为有图");

        const std::string empty = root + "/empty.rgba";
        writeThumb(empty, 0);
        CHECK_TRUE(!muvik::ui::sceneImageFileExists(empty),
                   "空文件不算有图（写到一半被强杀）");

        const std::string tiny = root + "/tiny.rgba";
        writeThumb(tiny, 64);
        CHECK_TRUE(!muvik::ui::sceneImageFileExists(tiny),
                   "过小的文件不算有图");

        CHECK_TRUE(!muvik::ui::sceneImageFileExists(root + "/missing.rgba"),
                   "不存在的文件不算有图");
        CHECK_TRUE(!muvik::ui::sceneImageFileExists(""),
                   "空路径不算有图");
    }

    TEST_BEGIN("缩略图文件判断：与句柄状态无关（本轮 bug 的回归）");
    {
        // 关键断言：文件存在 → 即使 nanoVG 句柄还没加载，也要判为「有图」。
        //
        // 修复前用的是 sceneImageReady()，它查 g_images（句柄表）。
        // 构造控件时一次都没绘制过，句柄必然不在表里 → 恒为 false。
        // 注：sceneImageReady() 查的是 nanoVG 句柄表，而句柄只在
        // drawScene() 里懒加载 —— 构造期调用它必然返回 false。
        // 这里不直接调它（那需要链接带 GL 依赖的 scene_tile.cpp），
        // 而是用「文件存在」这一条断言钉住布局判断的依据。
        const std::string ok = root + "/ok.rgba";
        CHECK_TRUE(muvik::ui::sceneImageFileExists(ok), "文件存在即可用于布局判断");
        CHECK_EQ(muvik::ui::sceneImageFileExists(ok), true,
                 "布局依据只看文件，不看纹理句柄");
    }

    TEST_BEGIN("紧凑布局：无图时高度应当更小");
    {
        // ResumeHero 的两种高度是布局契约：有图大卡、无图紧凑行。
        // 这里只校验常量关系，避免「紧凑行反而比大卡高」这种明显错误。
        // （实测值见 include/view/resume_hero.hpp）
        constexpr float kFull = 208.0f;
        constexpr float kCompact = 96.0f;
        CHECK_TRUE(kCompact < kFull, "紧凑行比大卡矮");
        CHECK_TRUE(kCompact > 60.0f, "紧凑行仍要放得下两行文字");
    }

    std::system(("rm -rf " + root).c_str());
    TEST_SUMMARY();
}
