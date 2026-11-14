#pragma once

#include "model/network_source.hpp"
#include "model/media_item.hpp"

#include <string>
#include <vector>

namespace muvik {

class WebDAVClient {
public:
    // 测试网络连接与账号认证
    static bool testConnection(const NetworkSource& source, std::string& err_msg);

    // 列出指定路径下的文件与子文件夹
    static bool listDirectory(const NetworkSource& source,
                              const std::string& path,
                              std::vector<MediaItem>& out_items,
                              std::string& err_msg);

    // 将媒体路径解析为 libmpv 可直接播放的串流 URL（嵌入凭证）
    static std::string buildPlayUrl(const NetworkSource& source, const std::string& path);

    // 辅助工具：URL 百分号编解码
    static std::string urlDecode(const std::string& in);
    static std::string urlEncode(const std::string& in);

private:
    static bool isMediaExtension(const std::string& filename);
};

} // namespace muvik
