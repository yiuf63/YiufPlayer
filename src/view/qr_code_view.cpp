#include "view/qr_code_view.hpp"
#include "utils/qrcodegen.hpp"
#include "ui/design.hpp"
#include <borealis/views/dialog.hpp>
#include <borealis/core/box.hpp>
#include <borealis/views/label.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <cmath>

namespace muvik {

QRCodeView::QRCodeView(std::string text, float qr_dimension)
    : text_(std::move(text)), qr_dimension_(qr_dimension) {
    setWidth(qr_dimension_);
    setHeight(qr_dimension_);
    regenerate();
}

void QRCodeView::setText(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        regenerate();
    }
}

void QRCodeView::regenerate() {
    modules_.clear();
    qr_size_ = 0;
    valid_ = false;

    if (text_.empty()) return;

    try {
        using qrcodegen::QrCode;
        QrCode qr = QrCode::encodeText(text_.c_str(), QrCode::Ecc::MEDIUM);
        qr_size_ = qr.getSize();
        modules_.resize(static_cast<size_t>(qr_size_ * qr_size_));

        for (int y = 0; y < qr_size_; ++y) {
            for (int x = 0; x < qr_size_; ++x) {
                modules_[static_cast<size_t>(y * qr_size_ + x)] = qr.getModule(x, y);
            }
        }
        valid_ = true;
    } catch (const std::exception& e) {
        brls::Logger::error("QRCodeView 生成二维码失败: {}", e.what());
    }
}

void QRCodeView::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style /*style*/, brls::FrameContext* /*ctx*/) {
    // 1. 绘制带圆角的高对比度纯白底卡
    const float side = std::min(width, height);
    const float rx = x + (width - side) * 0.5f;
    const float ry = y + (height - side) * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, rx, ry, side, side, ui::radius::md);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgFill(vg);

    if (!valid_ || qr_size_ <= 0) {
        nvgBeginPath(vg);
        nvgFontSize(vg, 14.0f);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(120, 120, 120));
        nvgText(vg, rx + side * 0.5f, ry + side * 0.5f, "二维码生成失败", nullptr);
        return;
    }

    // 2. 留出留白边界 (Quiet Zone)
    const float quiet_margin = side * 0.06f;
    const float active_size = side - quiet_margin * 2.0f;
    const float cell_size = active_size / static_cast<float>(qr_size_);

    // 3. 批量绘制所有黑色方块模块
    nvgBeginPath(vg);
    for (int qy = 0; qy < qr_size_; ++qy) {
        for (int qx = 0; qx < qr_size_; ++qx) {
            if (modules_[static_cast<size_t>(qy * qr_size_ + qx)]) {
                const float cx = rx + quiet_margin + static_cast<float>(qx) * cell_size;
                const float cy = ry + quiet_margin + static_cast<float>(qy) * cell_size;
                // 模块边界做轻微像素外扩防止渲染接缝
                nvgRect(vg, std::floor(cx), std::floor(cy),
                        std::ceil(cell_size) + 0.5f, std::ceil(cell_size) + 0.5f);
            }
        }
    }
    nvgFillColor(vg, nvgRGB(16, 18, 20)); // 深墨黑
    nvgFill(vg);
}

void QRCodeView::showDialog(const std::string& title,
                            const std::string& qr_text,
                            const std::string& hint_text) {
    auto* container = new brls::Box();
    container->setAxis(brls::Axis::COLUMN);
    container->setAlignItems(brls::AlignItems::CENTER);
    container->setJustifyContent(brls::JustifyContent::CENTER);
    container->setWidth(540.0f);
    container->setPadding(24.0f, 20.0f, 20.0f, 20.0f);

    // 标题
    if (!title.empty()) {
        auto* title_label = new brls::Label();
        title_label->setText(title);
        title_label->setFontSize(22.0f);
        title_label->setTextColor(ui::palette().text);
        title_label->setMarginBottom(16.0f);
        container->addView(title_label);
    }

    // 二维码视图（260x260）
    auto* qr = new QRCodeView(qr_text, 260.0f);
    qr->setMarginBottom(16.0f);
    container->addView(qr);

    // 提示文本
    if (!hint_text.empty()) {
        auto* hint_label = new brls::Label();
        hint_label->setText(hint_text);
        hint_label->setFontSize(14.0f);
        hint_label->setTextColor(ui::palette().textMute);
        hint_label->setMarginBottom(12.0f);
        container->addView(hint_label);
    }

    // 对话框
    auto* dialog = new brls::Dialog(container);
    dialog->addButton("关闭", []() {});
    dialog->open();
}

} // namespace muvik
