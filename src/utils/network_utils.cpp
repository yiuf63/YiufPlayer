#include "utils/network_utils.hpp"
#include <cstring>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace muvik {

std::string NetworkUtils::getLocalIPv4() {
#if defined(__unix__) || defined(__APPLE__)
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return "127.0.0.1";
    }

    std::string candidate_wlan;
    std::string candidate_eth;
    std::string candidate_other;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue; // 仅 IPv4

        // 排除 Loopback 和未启动的网卡
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        char ip_buf[INET_ADDRSTRLEN];
        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (!inet_ntop(AF_INET, &(sa->sin_addr), ip_buf, sizeof(ip_buf))) continue;

        std::string name = ifa->ifa_name ? ifa->ifa_name : "";
        std::string ip = ip_buf;

        // 优先无线网卡 (wlan0, en0, wl...)
        if (name.rfind("wlan", 0) == 0 || name.rfind("wl", 0) == 0 || name == "en0") {
            if (candidate_wlan.empty()) candidate_wlan = ip;
        } else if (name.rfind("eth", 0) == 0 || name.rfind("en", 0) == 0) {
            if (candidate_eth.empty()) candidate_eth = ip;
        } else {
            if (candidate_other.empty()) candidate_other = ip;
        }
    }

    freeifaddrs(ifaddr);

    if (!candidate_wlan.empty()) return candidate_wlan;
    if (!candidate_eth.empty()) return candidate_eth;
    if (!candidate_other.empty()) return candidate_other;
#endif

    return "127.0.0.1";
}

} // namespace muvik
