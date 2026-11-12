#pragma once

#include <string>
#include <string>
#include <cstdint>

namespace muvik {

// 从媒体文件名推断用于刮削的搜索词与年份。
//
// 典型输入：
//   "The.Matrix.1999.1080p.BluRay.x264-GROUP.mkv"
// 输出：
//   query = "The Matrix"
//   year  = 1999
//
// 该逻辑独立成模块以便单元测试（原先内嵌在 UI 代码中，无法测试）。
class MediaNameParser {
public:
    // 解析文件名。out_query 为清洗后的搜索词，out_year 为 0 表示未找到年份。
    static void parse(const std::string& filename,
                      std::string& out_query,
                      int64_t& out_year);

    // 便捷版本：只取搜索词
    static std::string queryFromFilename(const std::string& filename);
};

} // namespace muvik
