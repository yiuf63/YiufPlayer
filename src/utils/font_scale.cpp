#include "utils/font_scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "ui/design.hpp"
#include "utils/app_paths.hpp"

namespace muvik {

namespace {

// 配置文件的键。「key=value」逐行，与项目其它设置文件一致。
constexpr const char* kKey = "font_scale";

} // namespace

const std::vector<FontScale::Option>& FontScale::options() {
    // 三档的取值不是「小中大」的直觉值，而是按实际受限条件定的：
    //   0.85 —— 下限。caption 12.5 × 0.85 ≈ 10.6，再小在 640×360 下糊成一团。
    //   1.00 —— 标准，设计基准。
    //   1.20 —— 上限。再大则列表一行放不下两个中文词，卡片标题全部截断，
    //           扫描目录的完整路径也会被省略号吃掉。
    static const std::vector<Option> opts = {
        {0.85f, "small", "紧凑", "小屏 / 远距离，一屏多放几项"},
        {1.00f, "normal", "标准", "默认，按 40cm 握持距离设计"},
        {1.20f, "large", "大字", "近距 / 视力不便，优先看清文字"},
    };
    return opts;
}

std::string FontScale::filePath() {
    return AppPaths::configFile("ui_settings.dat");
}

float FontScale::resolve() {
    float scale = 1.0f;   // 默认标准

    // 配置文件的映射：按稳定 key 查，不按下标 ——
    // 将来增删档位时，下标会指向别的档。
    std::ifstream in(filePath());
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (line.substr(0, eq) != kKey) continue;
            const std::string val = line.substr(eq + 1);
            for (const auto& o : options()) {
                if (val == o.key) { scale = o.scale; break; }
            }
        }
    }

    // 环境变量优先（测试与调试用）。
    if (const char* env = std::getenv("MUVIK_FONT_SCALE")) {
        const float v = static_cast<float>(std::atof(env));
        if (v >= 0.5f && v <= 2.0f) scale = v;
    }

    ui::font::setScale(scale);
    return ui::font::scale();
}

float FontScale::current() {
    return ui::font::scale();
}

void FontScale::set(float scale, bool persist) {
    ui::font::setScale(scale);
    if (persist) save();
}

void FontScale::cycle() {
    const auto& opts = options();
    size_t idx = currentIndex();
    idx = (idx + 1) % opts.size();
    set(opts[idx].scale);
}

void FontScale::save() {
    AppPaths::ensureDir(AppPaths::configDir());

    // 只写这一个键：ui_settings.dat 将来可能还有别的界面设置，
    // 整文件覆写会抹掉别人的键。先读再改。
    std::vector<std::string> lines;
    {
        std::ifstream in(filePath());
        if (in.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.rfind(std::string(kKey) + "=", 0) == 0) continue;
                if (!line.empty()) lines.push_back(line);
            }
        }
    }
    const auto& opts = options();
    lines.push_back(std::string(kKey) + "=" + opts[currentIndex()].key);

    std::ofstream out(filePath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << "# Muvik UI 设置\n";
    for (const auto& l : lines) {
        if (l[0] == '#') continue;   // 头注释由上面统一写
        out << l << '\n';
    }
}

size_t FontScale::currentIndex() {
    const auto& opts = options();
    const float cur = current();
    size_t best = 0;
    float bestDiff = std::abs(opts[0].scale - cur);
    for (size_t i = 1; i < opts.size(); ++i) {
        const float d = std::abs(opts[i].scale - cur);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

} // namespace muvik
