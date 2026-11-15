#pragma once

#include <borealis/core/view.hpp>
#include <string>

namespace muvik {

// 矢量绘制的二维码视图（基于 qrcodegen + NanoVG）
class QRCodeView : public brls::View {
public:
    explicit QRCodeView(std::string text, float qr_dimension = 240.0f);
    ~QRCodeView() override = default;

    void setText(const std::string& text);
    const std::string& getText() const { return text_; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    // 弹出标准的扫码对话框
    static void showDialog(const std::string& title,
                           const std::string& qr_text,
                           const std::string& hint_text = "");

private:
    std::string text_;
    float qr_dimension_ = 240.0f;
    bool valid_ = false;
    std::vector<bool> modules_;
    int qr_size_ = 0;

    void regenerate();
};

} // namespace muvik
