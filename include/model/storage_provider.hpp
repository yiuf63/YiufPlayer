#pragma once

#include <memory>
#include <string>
#include <vector>
#include "model/media_item.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace deck_player {

// 存储扩展接口（所有本地存储、网络源 MOD 均实现此统一接口）
class IStorageProvider {
public:
    virtual ~IStorageProvider() = default;

    // 插件/源的唯一标识符（如 "local", "webdav", "smb", "alist"）
    virtual std::string getId() const = 0;

    // 显示名称（如 "WebDAV 网盘", "局域网 SMB 共享"）
    virtual std::string getDisplayName() const = 0;

    // 描述或连接地址信息
    virtual std::string getDescription() const = 0;

    // 图标名称
    virtual std::string getIconName() const = 0;

    // 是否需要鉴权/网络支持
    virtual bool isNetworkSource() const = 0;

    // 列出指定路径下的文件/文件夹
    virtual std::vector<MediaItem> listItems(const std::string& path) = 0;

    // 测试连接是否有效
    virtual bool testConnection() = 0;
};

// 插件注册中心（用于解耦主程序与收费/第三方 MOD 扩展）
class StorageProviderRegistry {
public:
    static StorageProviderRegistry& getInstance();

    // 注册存储源（可以是内置本地源，也可以是动态加载的 MOD 扩展）
    void registerProvider(std::shared_ptr<IStorageProvider> provider);

    // 获取所有已注册的存储源
    std::vector<std::shared_ptr<IStorageProvider>> getProviders() const;

    // 根据 ID 获取存储源
    std::shared_ptr<IStorageProvider> getProvider(const std::string& id) const;

    // 动态扫描并加载外部 MOD 插件 (~/.config/deck-player/plugins/*.so)
    void loadExternalPlugins(const std::string& plugins_dir = "");

private:
    StorageProviderRegistry();
    std::vector<std::shared_ptr<IStorageProvider>> providers_;
};

} // namespace deck_player
