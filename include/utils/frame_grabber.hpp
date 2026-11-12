#pragma once

#include <string>
#include <string>

namespace muvik {

// 帧缓冲抓取工具。
//
// 用途：在无法使用系统截图（macOS 屏幕录制权限受限、显示器休眠、
// CI 环境无显示）时，直接从 OpenGL 帧缓冲读取画面并写入文件，
// 用于验证 UI 是否真的渲染出内容。
//
// 仅在设置了环境变量 MUVIK_FRAMEGRAB=<输出路径.ppm> 时启用。
//
// 支持连续抓取（用于验证视频是否真的在播放）：
//   MUVIK_FRAMEGRAB=/tmp/f.ppm        抓到 1 帧后退出
//   MUVIK_FRAMEGRAB_COUNT=5           连续抓 5 帧，文件名加 _0.._4 后缀
//   MUVIK_FRAMEGRAB_INTERVAL=30       每 30 帧抓一次（约 0.5 秒）
//   MUVIK_FRAMEGRAB_EXIT=0            抓完不退出，继续运行
//   MUVIK_FRAMEGRAB_START=180         延迟到第 180 帧才开始抓（约 3 秒）
//                                          验证视频播放时必须设大一些，
//                                          否则 mpv 还没解码出首帧
//
// 输出 PPM（P6）格式：结构简单、无需第三方库即可生成与分析。
class FrameGrabber {
public:
    // 是否启用（依据环境变量判断）
    static bool enabled();

    // 在主循环中调用。内部按帧计数，达到目标帧数后写出文件。
    // 返回 true 表示已完成抓取（调用方可以选择退出程序）。
    static bool tick(int frame_index);

    // 目标输出路径（未启用时为空）
    static std::string outputPath();

private:
    static bool writePPM(const std::string& path, int width, int height,
                         const unsigned char* rgb_flipped);
};

} // namespace muvik
