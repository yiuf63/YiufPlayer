// 首页「继续观看」主视觉。
//
// 视觉：一张横向宽图（16:9 裁切），左下压片名与集号，右下给进度与时长。
// 无缩略图时收成一条紧凑行 —— 这不是降级方案，是为 thin 包做的**第一等状态**。

#include "view/resume_hero.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <cinttypes>
#include <cstdio>

#include "ui/design.hpp"
#include "ui/icons.hpp"
#include "view/media_thumb.hpp"
#include "utils/scanner.hpp"

namespace muvik {

namespace {

// 焦点过渡时长，与 MediaCell 一致（120ms）。
constexpr float kFocusMs = 120.0f;

std::string episodeLabel(const ResumeHeroSpec& s) {
    // 真实季集号优先。缺集时（S01E05 + S01E08）用下标会写成「第 2 集」，
    // 用户会以为文件放错了 —— 见交接文档 01-findings.md。
    if (s.episode > 0) {
        if (s.season > 0)
            return "第 " + std::to_string(s.season) + " 季 第 " +
                   std::to_string(s.episode) + " 集";
        return "第 " + std::to_string(s.episode) + " 集";
    }
    return {};
}

std::string timeLabel(int64_t sec) {
    if (sec <= 0) return "00:00";
    const int64_t h = sec / 3600;
    const int64_t m = (sec % 3600) / 60;
    const int64_t s = sec % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%" PRId64 ":%02" PRId64 ":%02" PRId64, h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%02" PRId64 ":%02" PRId64, m, s);
    return buf;
}

} // namespace

ResumeHero::ResumeHero(ResumeHeroSpec spec) : spec_(std::move(spec)) {
    art_ = sceneSpecFor(spec_.item);
    // 用「文件是否存在」判断，不能用 sceneImageReady ——
    // 后者查的是 nanoVG 句柄，而句柄要到 drawScene() 才懒加载，
    // 这里必然还没加载过，会永远判成无图（实测踩过）。
    has_art_ = ui::sceneImageFileExists(art_.thumb_path);

    setFocusable(true);
    setHeight(has_art_ ? HEIGHT : COMPACT_H);
}

void ResumeHero::setFocused(bool focused) {
    if (focused_ == focused) return;
    focused_ = focused;
    focus_t0_ = std::chrono::steady_clock::now();
}

void ResumeHero::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;

    const auto& pal = ui::palette();

    // ── 焦点插值 ──
    const float dt = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - focus_t0_).count();
    const float target = brls::Application::getCurrentFocus() == this ? 1.0f : 0.0f;
    const float step = std::clamp(dt / kFocusMs, 0.0f, 1.0f);
    focus_anim_ = focus_anim_ + (target - focus_anim_) * ui::easeOut(step);
    if (std::abs(target - focus_anim_) < 0.004f) focus_anim_ = target;
    const float fa = focus_anim_;

    const float r = ui::radius::lg;

    if (has_art_) {
        // ── 有图：大卡 ──
        ui::drawScene(vg, x, y, width, height, r, r, r, r, art_);

        // 底部压暗：片名要压在画面上，必须保证可读。
        // 用渐变而不是纯色块 —— 纯色会在画面中间切出一条硬边。
        const float gradH = height * 0.62f;
        NVGpaint g = nvgLinearGradient(vg, x, y + height - gradH, x, y + height,
                                       nvgRGBAf(0, 0, 0, 0.0f),
                                       nvgRGBAf(0, 0, 0, 0.86f));
        nvgBeginPath(vg);
        nvgRect(vg, x, y + height - gradH, width, gradH);
        nvgFillPaint(vg, g);
        nvgFill(vg);

        const float pad = ui::space::xl;
        float ty = y + height - pad;

        // 进度与时长（右下）
        const float ratio = (spec_.duration > 0)
            ? std::clamp(static_cast<float>(spec_.position) /
                         static_cast<float>(spec_.duration), 0.0f, 1.0f)
            : 0.0f;
        if (spec_.can_resume && spec_.duration > 0) {
            const std::string t2 = timeLabel(spec_.duration);
            const float t2w = ui::textWidth(vg, ui::font::caption(), t2);
            ui::text(vg, x + width - pad - t2w, ty - 14.0f, ui::font::caption(),
                     ui::rgba(pal.textDim, 0.9f), NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, t2);

            const float barW = std::min(260.0f, width * 0.34f);
            const float barX = x + width - pad - barW;
            const float barY = ty - 6.0f;
            ui::fillRoundRect(vg, barX, barY, barW, 3.0f, 1.5f,
                              ui::rgba(pal.text, 0.18f));
            ui::fillRoundRect(vg, barX, barY, barW * ratio, 3.0f, 1.5f, pal.accent);
        }

        // 片名（左下）
        const std::string title = spec_.title.empty() ? spec_.item.name : spec_.title;
        const float titleMaxW = width - pad * 2;
        ui::textClipped(vg, x + pad, ty, ui::font::display(), pal.text,
                        NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, title, titleMaxW);

        // 副标题一行：集号 · 状态
        std::string sub;
        const std::string ep = episodeLabel(spec_);
        if (!ep.empty()) sub += ep;
        if (spec_.can_resume) {
            if (!sub.empty()) sub += " · ";
            sub += "已看 " + timeLabel(spec_.position) + " / " + timeLabel(spec_.duration);
        } else {
            if (!sub.empty()) sub += " · ";
            sub += "从头播放";
        }
        if (!spec_.series_title.empty()) {
            sub += " · ";
            sub += spec_.series_title;
        }
        ui::textClipped(vg, x + pad, ty - 26.0f, ui::font::body(),
                        ui::rgba(pal.textDim, 0.95f),
                        NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, sub, titleMaxW);

        // 焦点：不用圆角描边，用左侧强调条 + 整体提亮。
        // 大卡上套一圈描边会像"图片没加载完的错误框"。
        if (fa > 0.01f) {
            ui::strokeRoundRect(vg, x, y, width, height, r,
                                ui::rgba(pal.accent, 0.55f * fa), 2.0f);
        }
    } else {
        // ── 无图：紧凑行 ──
        //
        // thin 包（SteamOS / TrimUI）默认关闭抽帧，这是它们的**正常状态**。
        // 这里不画占位色块：一条干净的深色行 + 完整片名，比"一张假封面"
        // 更容易读，也不会让人以为图加载失败。
        ui::fillRoundRect(vg, x, y, width, height, ui::radius::md,
                          ui::mix(ui::rgba(pal.surface, 1.0f),
                                  ui::rgba(pal.accent, 0.6f),
                                  fa * 0.22f));

        const float pad = ui::space::lg;
        const float iconR = 22.0f;
        const float cx = x + pad + iconR;
        const float cy = y + height * 0.5f;
        ui::fillCircle(vg, cx, cy, iconR,
                       ui::rgba(pal.accent, 0.14f + 0.10f * fa));
        ui::drawIcon(vg, ui::Icon::Play, cx, cy, 13.0f + fa, pal.accent, 2.1f);

        const float tx = cx + iconR + ui::space::lg;
        const float tw = width - (tx - x) - pad;

        const std::string title = spec_.title.empty() ? spec_.item.name : spec_.title;
        ui::textClipped(vg, tx, y + height * 0.5f - 4.0f, ui::font::title(),
                        pal.text, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, title, tw);

        std::string sub;
        const std::string ep = episodeLabel(spec_);
        if (!ep.empty()) sub += ep;
        if (spec_.can_resume) {
            if (!sub.empty()) sub += " · ";
            sub += "已看 " + timeLabel(spec_.position) + " / " + timeLabel(spec_.duration);
        } else {
            if (!sub.empty()) sub += " · ";
            sub += "从头播放";
        }
        ui::textClipped(vg, tx, y + height * 0.5f + 20.0f, ui::font::detail(),
                        pal.textDim, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, sub, tw);

        // 进度条贴着行的底部
        if (spec_.can_resume && spec_.duration > 0) {
            const float ratio = std::clamp(
                static_cast<float>(spec_.position) / static_cast<float>(spec_.duration),
                0.0f, 1.0f);
            ui::fillRoundRect(vg, x, y + height - 3.0f, width, 3.0f, 0.0f,
                              ui::rgba(pal.text, 0.10f));
            ui::fillRoundRect(vg, x, y + height - 3.0f, width * ratio, 3.0f, 0.0f,
                              pal.accent);
        }

        if (fa > 0.01f) {
            ui::strokeRoundRect(vg, x, y, width, height, ui::radius::md,
                                ui::rgba(pal.accent, fa), 2.0f);
        }
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

} // namespace muvik
