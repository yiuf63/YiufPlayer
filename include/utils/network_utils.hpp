#pragma once

#include <string>

namespace muvik {

class NetworkUtils {
public:
    // 获取本机首选局域网 IPv4 地址（如 192.168.x.x），无网络时返回 127.0.0.1
    static std::string getLocalIPv4();
};

} // namespace muvik
