#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <borealis.hpp>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <string>
#include <vector>
#include <functional>
#include "model/media_item.hpp"

namespace deck_player {

enum class GestureMode {
    None,
    Brightness,
    Volume,
    Seek
};

struct TrackInfo {
    int id = 0;
    std::string type;   // "video", "audio", "sub"
    std::string title;
    std::string lang;
    std::string codec;  // 如 subrip / ass / hdmv_pgs_subtitle，用于区分图形字幕
    bool selected = false;
    bool external = false; // true = 外挂字幕文件，false = 内嵌于容器

    // 是否为图形字幕（蓝光 PGS / DVD SUB）。
    // 这类字幕本质是位图，无法转文本，但 mpv 可直接渲染。
    bool is_bitmap() const {
        return codec == "hdmv_pgs_subtitle" || codec == "dvd_subtitle" ||
               codec == "dvb_subtitle" || codec == "xsub";
    }

    // 展示用名称：优先 title，其次语言，最后回落到轨道号
    std::string displayName() const {
        std::string out;
        if (!title.empty()) out = title;
        if (!lang.empty()) {
            if (!out.empty()) out += " [" + lang + "]";
            else out = lang;
        }
        if (out.empty()) out = "轨道 #" + std::to_string(id);
        return out;
    }

    // 类型标记，用于界面区分内嵌 / 外挂
    std::string sourceLabel() const {
        return external ? "外挂" : "内嵌";
    }
};

class MPVPlayerView : public brls::Box {
public:
    MPVPlayerView();
    ~MPVPlayerView() override;

    // 打开播放文件
    void openFile(const std::string& path, const std::string& title = "", int64_t resume_position = 0);

    // 基础播放控制
    void togglePlayPause();
    void play();
    void pause();
    void seek(int64_t seconds_offset);
    void seekTo(int64_t target_seconds);

    // 播放倍速 (0.5x ~ 3.0x 循环切换)
    void cyclePlaybackSpeed();
    void setPlaybackSpeed(double speed);
    double getPlaybackSpeed() const;

    // 画面比例切换 (自适应, 16:10满屏, 16:9, 4:3, 裁剪)
    void cycleAspectRatio();
    void setAspectRatio(AspectRatioMode mode);

    // 锁屏防误触
    void toggleTouchLock();
    bool isTouchLocked() const { return touch_locked_; }

    // 音量与亮度
    void setVolume(int volume_0_100);
    int getVolume() const;
    void setBrightness(float brightness_0_1);
    float getBrightness() const;

    // 音视频与字幕轨道控制
    std::vector<TrackInfo> getTracks(const std::string& type);
    void selectTrack(const std::string& type, int track_id);
    void setSubtitleDelay(float seconds);
    float getSubtitleDelay() const { return subtitle_delay_; }

    // 加载外部字幕文件（对话框选择路径后调用）
    void loadExternalSubtitle(const std::string& path);

    // 回调事件
    void setOnFinishCallback(std::function<void()> cb) { on_finish_cb_ = cb; }
    void setOnPrevNextCallback(std::function<void(bool is_next)> cb) { on_prev_next_cb_ = cb; }

    // 打开播放列表（由 PlayerActivity 提供内容与跳转能力）。
    // 若未设置，则按「播放列表」键不会有反应——单文件播放时也没有列表可显示。
    void setOnShowPlaylistCallback(std::function<void()> cb) { on_show_playlist_cb_ = cb; }

    // Borealis 绘制与事件
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override { return this; }

private:
    // 帧末渲染（在 nanoVG 提交之后执行，避免被覆盖）
    void renderMPV();
    void blitVideoTexture(int dst_x, int dst_y, int dst_w, int dst_h);
    void drawOverlay();
    void drawHud(NVGcontext* vg, float x, float y, float width, float height);

    // 触摸控件：触屏用户无法用 A/X/Y 键，必须有一排可点按钮。
    enum class TouchAction {
        PlayPause, SeekBack, SeekForward,
        Prev, Next,            // 上一集 / 下一集
        AudioTrack, SubtitleTrack,
        Aspect,                // 画面比例
        Speed,                 // 倍速
        Lock,                  // 防误触锁
        Playlist,              // 播放列表
        Back                   // 退出播放页
    };
    struct HitRect { float x, y, w, h; TouchAction action; };

    // 进度条拖动。
    // 触摸判定区比可视条高得多（可视 8px，判定 44px），
    // 否则手指根本点不中——这是「进度条只能看不能拖」之外的第二层问题。
    struct SeekBarRect {
        float x = 0, y = 0, w = 0, h = 0;   // 可视区域
        float hit_y = 0, hit_h = 0;         // 触摸判定区（以可视条为中心加高）
        bool valid() const { return w > 0.0f && h > 0.0f; }
    };
    SeekBarRect seek_bar_;
    bool seek_dragging_ = false;      // 正在拖动
    double seek_preview_ = 0.0;       // 拖动中的预览位置（不立即 seek）
    double seek_preview_total_ = 0.0; // 拖动开始时的总时长，避免中途变化

    // 把屏幕 x 坐标换算成目标时间（秒）
    double seekBarTimeAt(float px) const;
    // drawTouchControls 每次重绘时刷新 hit_rects_，
    // 与屏幕上的按钮位置严格对应（窗口尺寸变化后自动更新）。
    std::vector<HitRect> hit_rects_;
    // 当前「按下未松手」的按钮，用于绘制按下态（描边加粗）。
    TouchAction locked_action_ = TouchAction::Back;   // 仅当 has_locked_action_ 为真时有效
    bool has_locked_action_ = false;

    // 长按加速：按住屏幕不动约 0.45 秒 → 临时提速，松手恢复。
    // 这是所有成熟播放器都有、而本项目此前缺失的基础功能
    // （跳过片头/快进广告时非常依赖它）。
    bool touch_down_ = false;                 // 手指是否还按着
    bool press_moved_ = false;                // 按下后是否已滑动（滑动就不算长按）
    bool hold_boosting_ = false;              // 当前是否处于长按加速中
    double speed_before_boost_ = 1.0;         // 用于松手后恢复
    std::chrono::steady_clock::time_point press_down_time_{};
    void updateHoldBoost();   // 每帧调用：到达阈值才真正提速
    void beginHoldBoost();
    void endHoldBoost();

    // 上一次点击的落点，用于双击判定（要求两次位置接近）。
    brls::Point last_touch_pos_{};
    void drawTouchControls(NVGcontext* vg, float x, float y, float width, float height,
                           float dy = 0.0f);
    bool hitTouchControls(float px, float py);
    void performTouchAction(TouchAction action);

    struct DrawRect { float x = 0, y = 0, w = 0, h = 0; };
    DrawRect last_draw_;
    bool pending_render_ = false;

    // 离屏渲染资源：mpv 先画到纹理，再由 nanoVG 把纹理合成到界面，
    // 这样视频就被当作普通图层，UI（OSD / 进度条）可以正常叠加在上面。
    unsigned int vid_fbo_ = 0;
    unsigned int vid_tex_ = 0;
    int vid_tex_w_ = 0;
    int vid_tex_h_ = 0;
    int nvg_image_ = 0;
    bool ensureVideoTarget(int w, int h);
    void releaseVideoTarget();

    void initMPV();
    void destroyMPV();
    void checkMPVEvents();
    void showHUD(const std::string& icon, const std::string& text, float progress = -1.0f);
    std::string getCurrentTimeStr() const;

    // 触控手势
    void setupGestures();
    void setupGestureSelfTest();
    void updateTouchUI();
    void promptLoadExternalSubtitle();

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* mpv_gl_ = nullptr;

    std::string current_file_path_;
    std::string video_title_;
    bool is_playing_ = false;
    int64_t current_position_ = 0; // 秒
    int64_t total_duration_ = 0;   // 秒
    int volume_ = 100;
    float brightness_ = 1.0f;
    // 平台有没有真正的背光接口（Steam Deck / 安卓 / Switch 有，桌面没有）。
    // 有就直接调系统背光，没有才退回「叠一层黑色蒙层」的模拟。
    bool backlight_capable_ = false;
    double playback_speed_ = 1.0;
    AspectRatioMode aspect_mode_ = AspectRatioMode::Auto;
    bool touch_locked_ = false;
    float subtitle_delay_ = 0.0f;
    int selected_sid_ = 1;  // 用户选择的字幕轨（<=0 表示关闭）
    bool subtitle_chosen_ = false;  // 本次加载是否已自动选过字幕
    int selected_aid_ = 1;  // 用户选择的音轨
    int sub_pos_applied_ = 80;  // 已下发的字幕垂直位置（OSD 联动：显示时抬到控制栏上方）

    // 用于 OSD 顶部信息行（分辨率 / 硬解状态）。
    // 在 FILE_LOADED 时向 mpv 查询一次即可，不必每帧读属性。
    int video_w_ = 0;
    int video_h_ = 0;
    bool hwdec_active_ = false;
    // 触控锁定后轻点屏幕时，短暂显示「已锁定 · 双击解锁」提示。
    std::chrono::steady_clock::time_point lock_hint_until_{};

    // 触控手势状态
    GestureMode current_gesture_ = GestureMode::None;    brls::Point touch_start_point_;
    int64_t seek_start_pos_ = 0;
    int volume_start_ = 100;
    float brightness_start_ = 1.0f;
    std::chrono::steady_clock::time_point last_touch_down_time_;

    // 手势进行中的实时反馈值
    bool gesture_active_ = false;
    bool gesture_double_tap_pending_ = false;
    int64_t gesture_preview_position_ = 0;
    float gesture_accumulatedY_ = 0.0f;
    float gesture_accumulatedX_ = 0.0f;

    // OSD 悬浮显示状态
    bool osd_visible_ = true;
    // OSD 渐显动画（帧率无关：目标翻转时记录起点 alpha 与时刻，每帧按耗时插值）。
    // 背景：此前 osd_shown 是纯 bool，控制栏瞬间出现/消失——「直挺挺显示」。
    float osd_alpha_ = 1.0f;        // 当前透明度 0..1
    float osd_alpha_from_ = 1.0f;   // 本次动画的起点 alpha
    bool  osd_anim_target_ = true;  // 动画目标（true=显示）
    std::chrono::steady_clock::time_point osd_anim_t0_{};  // 动画开始时刻
    std::chrono::steady_clock::time_point last_user_action_time_;
    std::string hud_icon_;
    std::string hud_text_;
    float hud_progress_ = -1.0f;
    std::chrono::steady_clock::time_point hud_expire_time_;

    std::function<void()> on_finish_cb_;
    bool eof_fired_ = false;   // 防止 eof-reached 重复触发连播
    bool file_loaded_ = false;  // 已收到 FILE_LOADED，才接受 eof 信号
    std::chrono::steady_clock::time_point last_eof_time_{};  // 连播防抖
    std::function<void(bool)> on_prev_next_cb_;
    std::function<void()> on_show_playlist_cb_;
};

} // namespace deck_player
