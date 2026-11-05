#pragma once

// DeckPlayer 视觉设计系统（design tokens + 绘制基元）。
//
// 为什么要有这一层：
//   重构前每个界面各自写颜色字面量、字号字面量、圆角字面量，
//   于是同一屏里出现 22/21/20/17/15 五种字号和三种边距，
//   视觉上没有体系，观感就是「拼出来的」。
//   这里把「色板 / 间距 / 圆角 / 字号 / 阴影」集中成唯一来源，
//   所有界面（列表、播放器 OSD、播放条）都只引用这里的 token。
//
// 设计取向（对齐电视端影音软件，而不是游戏机系统菜单）：
//   · 表面分层：背景 → 卡片 → 悬停/选中，三级色阶差异足够但不刺眼
//   · 唯一强调色（暖橙），只在「主操作 / 选中 / 进度」上出现
//   · 停顿感：卡片用 14px 圆角、间距用 4 的倍数，避免所有元素贴在一起
//   · 文字只用大小和明度分级，不依赖粗体（项目字体只有一个字重）
//
// OLED 变体（A 屏黑）由 deck_theme 的 ThemeKind 决定，色板自动切换。

#include <string>

#include <borealis/core/theme.hpp>
#include <borealis/extern/nanovg/nanovg.h>

namespace deck_player::ui {

// ── 色板 ────────────────────────────────────────────────────────────────
// 字段按用途命名，而不是按颜色命名 —— 换配色时只改 cpp 里的常量。
struct Palette {
    NVGcolor bg;            // 页面背景
    NVGcolor surface;       // 一级卡片（列表行、面板）
    NVGcolor surfaceRaised; // 二级表面（悬停、弹层）
    NVGcolor surfaceSunken; // 凹陷（轨道底、占位块）
    NVGcolor outline;       // 细描边
    NVGcolor outlineStrong; // 强调描边（选中态）
    NVGcolor text;          // 主文字
    NVGcolor textDim;       // 副文字
    NVGcolor textMute;      // 辅助信息（时间、路径）
    NVGcolor accent;        // 强调色
    NVGcolor accentInk;     // 强调色上的文字（深色）
    NVGcolor accentSoft;    // 强调色低透明度（图标底、胶囊）
    NVGcolor danger;
    NVGcolor scrim;         // 浮层压暗底（浅色主题是暖白，见 kLight 注释）
    float scrimAlpha;       // 压暗底的使用浓度（深 0.55 / 浅 0.68）
};

const Palette& palette();

// ── 尺度 ────────────────────────────────────────────────────────────────
namespace space {
constexpr float xs  = 4.0f;
constexpr float sm  = 8.0f;
constexpr float md  = 12.0f;
constexpr float lg  = 16.0f;
constexpr float xl  = 24.0f;
constexpr float xxl = 32.0f;
} // namespace space

namespace radius {
constexpr float sm   = 8.0f;
constexpr float md   = 14.0f;
constexpr float lg   = 20.0f;
constexpr float pill = 999.0f;
} // namespace radius

// 字号（逻辑像素）——只有这五级，禁止再出现第六种。
namespace font {
constexpr float display = 26.0f; // 页面主标题
constexpr float title   = 20.0f; // 大标题 / 播放器片名
constexpr float body    = 17.0f; // 列表行主文字
constexpr float detail  = 14.0f; // 副文字 / 元信息
constexpr float caption = 12.5f; // 最小说明文字
} // namespace font

// ── 颜色工具 ────────────────────────────────────────────────────────────
NVGcolor rgba(NVGcolor c, float alpha);
NVGcolor mix(NVGcolor a, NVGcolor b, float t);
NVGcolor hex(unsigned int rgb, float alpha = 1.0f);

// ── 绘制基元 ────────────────────────────────────────────────────────────
void fillRoundRect(NVGcontext* vg, float x, float y, float w, float h,
                   float r, NVGcolor color);
void strokeRoundRect(NVGcontext* vg, float x, float y, float w, float h,
                     float r, NVGcolor color, float width = 1.0f);
void fillCircle(NVGcontext* vg, float cx, float cy, float r, NVGcolor color);
void strokeCircle(NVGcontext* vg, float cx, float cy, float r, NVGcolor color,
                  float width = 1.0f);

// 顶部 / 底部遮罩：三段落色，比两段线性渐变更接近电视端的「柔光压边」。
void scrimTop(NVGcontext* vg, float x, float y, float w, float h, float alpha);
void scrimBottom(NVGcontext* vg, float x, float y, float w, float h, float alpha);

// 胶囊标签（可选前置图标，返回实际宽度，便于排列）。
float drawPill(NVGcontext* vg, float x, float y, const std::string& label,
               float fontSize, NVGcolor bg, NVGcolor fg, float padX = 12.0f,
               float height = 28.0f);

// ── 文本 ────────────────────────────────────────────────────────────────
// 所有文字绘制都走这里：统一字体、统一对齐语义，避免漏设字体导致整块空白
// （历史 bug：nvgFontFace("sans") 未注册，文字全部不绘制）。
float textWidth(NVGcontext* vg, float size, const std::string& s);
void text(NVGcontext* vg, float x, float y, float size, NVGcolor color,
          int align, const std::string& s);
// 超宽时截断并补省略号（按 UTF-8 码点裁剪，不会切出半个汉字）。
std::string ellipsize(NVGcontext* vg, float size, const std::string& s, float maxW);
// 绘制并返回实际占用宽度。
float textClipped(NVGcontext* vg, float x, float y, float size, NVGcolor color,
                  int align, const std::string& s, float maxW);

} // namespace deck_player::ui
