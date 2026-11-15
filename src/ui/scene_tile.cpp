#include <string>
#include "ui/scene_tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "ui/design.hpp"
#include "ui/icons.hpp"

namespace muvik::ui {

namespace {

struct TilePalette {
    unsigned int skyTop;
    unsigned int skyBottom;
    unsigned int sun;
};

// 八套「天空 → 地面」配色。同一屏里 8 张卡共 32 格，靠这里的色相
// 和下面按哈希取的太阳位置拉开差异，不会读成一片同色色块。
const TilePalette kPalettes[] = {
    {0x3A5566, 0x16232C, 0xFFEBC6},  // 海港黄昏
    {0x2B3340, 0x11161D, 0xFFCF9B},  // 城市傍晚
    {0x24402F, 0x0E1A14, 0xD7F0B8},  // 林间晨光
    {0x43536B, 0x1A222E, 0xE8F1FF},  // 雪原
    {0x241E33, 0x0D0A14, 0xC9B6FF},  // 夜山
    {0x5A4A34, 0x221A12, 0xFFD9A0},  // 沙滩
    {0x2A2036, 0x100B17, 0xFF9F69},  // 都市霓虹
    {0x1D3A44, 0x08161B, 0x9FE6F0},  // 深海
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));

// 图像句柄缓存。nanoVG 的图片按 context 创建，但同一个 backend 下句柄可复用；
// 窗口重建会换 context，所以缓存里同时记 path，句柄失效时（画不出来）重载。
std::unordered_map<std::string, int> g_images;

// 缩略图缓存格式：4 字节 "DPT1" + uint16 宽 + uint16 高 + RGBA。
// 不用 PNG 是因为 nvgCreateImageRGBA 直接能吃裸像素，省掉一个编码器依赖。
int loadRgbaImage(NVGcontext* vg, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return -1;
    char magic[4] = {0};
    f.read(magic, 4);
    if (std::memcmp(magic, "DPT1", 4) != 0) return -1;
    uint16_t w = 0, h = 0;
    f.read(reinterpret_cast<char*>(&w), sizeof(w));
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (w < 8 || h < 8) return -1;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!f.good() && f.gcount() != static_cast<std::streamsize>(buf.size())) return -1;
    return nvgCreateImageRGBA(vg, w, h, 0, buf.data());
}

int imageHandle(NVGcontext* vg, const std::string& path) {
    auto it = g_images.find(path);
    if (it != g_images.end()) return it->second;

    int handle = loadRgbaImage(vg, path);
    if (handle <= 0) {
        // 不是本程序写的缓存（例如外部放了一张 PNG），交给 nanoVG 自己认
        handle = nvgCreateImage(vg, path.c_str(), 0);
    }
    // 失败不缓存：抽帧可能刚好在下一帧完成，缓存记住 -1 就永远画不出来了
    if (handle > 0) g_images[path] = handle;
    return handle;
}

// 一道山脊剪影。
//
// 这是「像画面」的关键：渐变 + 圆点只能读成色斑，人眼认画面靠的是
// **轮廓**。一条不规则的深色山脊压在渐变的天空上，缩到 80×70 也还是
// 「落日下的山」，而同样尺寸下纯渐变只是一块发光的雾。
void hillShape(NVGcontext* vg, float x, float y, float w, float h,
               float baseY, float amp, float phase01, NVGcolor color) {
    const float p1 = 0.18f + phase01 * 0.34f;
    const float p2 = p1 + 0.30f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, baseY + amp * 0.20f);
    nvgBezierTo(vg, x + w * p1 * 0.60f, baseY + amp * 0.10f,
                x + w * (p1 - 0.08f), baseY - amp * 0.90f,
                x + w * p1, baseY - amp);
    nvgBezierTo(vg, x + w * (p1 + 0.12f), baseY - amp * 1.05f,
                x + w * (p2 - 0.10f), baseY - amp * 0.25f,
                x + w * p2, baseY - amp * 0.35f);
    nvgBezierTo(vg, x + w * (p2 + 0.12f), baseY - amp * 0.15f,
                x + w * 0.92f, baseY - amp * 0.55f,
                x + w, baseY - amp * 0.30f);
    nvgLineTo(vg, x + w, y + h);
    nvgLineTo(vg, x, y + h);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPlaceholder(NVGcontext* vg, float x, float y, float w, float h,
                     float tl, float tr, float br, float bl, uint32_t seed) {
    const TilePalette& p = kPalettes[seed % kPaletteCount];
    const float jitter = static_cast<float>((seed >> 9) % 100) / 100.0f;
    const float jitter2 = static_cast<float>((seed >> 17) % 100) / 100.0f;

    NVGpaint sky = nvgLinearGradient(vg, x, y, x, y + h,
                                     hex(p.skyTop), hex(p.skyBottom));
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, w, h, tl, tr, br, bl);
    nvgFillPaint(vg, sky);
    nvgFill(vg);

    // 落日：压在远山后面，别放在画面正中
    const float sunR = std::max(3.0f, h * 0.13f);
    const float sunX = x + w * (0.30f + jitter * 0.42f);
    const float sunY = y + h * (0.50f + jitter2 * 0.10f);
    NVGpaint glow = nvgRadialGradient(vg, sunX, sunY, sunR * 0.5f, sunR * 3.6f,
                                      rgba(hex(p.sun), 0.75f),
                                      rgba(hex(p.sun), 0.0f));
    nvgBeginPath(vg);
    nvgCircle(vg, sunX, sunY, sunR * 3.6f);
    nvgFillPaint(vg, glow);
    nvgFill(vg);
    fillCircle(vg, sunX, sunY, sunR, hex(p.sun, 0.92f));

    // 远山 / 近山：远山更接近天空色，形成空气透视
    const NVGcolor far = mix(hex(p.skyBottom), hex(0x000000), 0.35f);
    const NVGcolor near = mix(hex(p.skyBottom), hex(0x000000), 0.70f);
    hillShape(vg, x, y, w, h, y + h * 0.68f, h * 0.26f, jitter, far);
    hillShape(vg, x, y, w, h, y + h * 0.88f, h * 0.16f, jitter2, near);
}

} // namespace

uint32_t hashString(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

bool sceneImageReady(const std::string& path) {
    return !path.empty() && g_images.find(path) != g_images.end() &&
           g_images[path] > 0;
}

void drawScene(NVGcontext* vg, float x, float y, float w, float h,
               float tl, float tr, float br, float bl, const SceneSpec& spec) {
    if (w < 8.0f || h < 8.0f) return;

    // 真实帧优先：等比例铺满 + 圆角裁剪。
    // 抽帧的宽高比与卡片不一定一致（16:9 的帧放进 16:9.4 的格子），
    // 用 cover 语义裁掉多出来的部分，而不是压扁。
    if (!spec.thumb_path.empty()) {
        const int img = imageHandle(vg, spec.thumb_path);
        if (img > 0) {
            int iw = 0, ih = 0;
            nvgImageSize(vg, img, &iw, &ih);
            if (iw > 0 && ih > 0) {
                // 直接用「圆角路径 + 图片填充」：填充天然按路径裁剪，
                // 不需要 nvgClip（nanoVG 里也没有这个函数，只有 nvgScissor，
                // 而 scissor 只能矩形，圆角会被切掉）
                const float scale = std::max(w / static_cast<float>(iw),
                                             h / static_cast<float>(ih));
                const float dw = iw * scale;
                const float dh = ih * scale;
                NVGpaint paint = nvgImagePattern(vg, x + (w - dw) * 0.5f,
                                                 y + (h - dh) * 0.5f, dw, dh,
                                                 0.0f, img, 1.0f);
                nvgBeginPath(vg);
                nvgRoundedRectVarying(vg, x, y, w, h, tl, tr, br, bl);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
            }
        }
    } else {
        drawPlaceholder(vg, x, y, w, h, tl, tr, br, bl, spec.seed);
    }

    // 音频角标：音频没有「画面」，光靠配色分不出来
    if (spec.audio && w >= 44.0f) {
        const float chip = std::min(20.0f, h * 0.42f);
        const float chipX = x + 6.0f;
        const float chipY = y + h - chip - 6.0f;
        fillRoundRect(vg, chipX, chipY, chip, chip, 6.0f, hex(0x000000, 0.6f));
        drawIcon(vg, Icon::Music, chipX + chip * 0.5f, chipY + chip * 0.5f,
                 chip * 0.62f, hex(0xE7EDF5, 0.92f), 1.8f);
    }
}

} // namespace muvik::ui
