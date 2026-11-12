#include <memory>
#include <string>
#include <vector>
#include "model/storage_provider.hpp"
#include "utils/scanner.hpp"
#include "utils/app_paths.hpp"
#include <filesystem>
#include <dlfcn.h>
#include <cstdlib>
#include <iostream>

namespace fs = std::filesystem;

namespace muvik {

// 内置的基础本地存储提供者
class LocalStorageProvider : public IStorageProvider {
public:
    std::string getId() const override { return "local_fs"; }
    std::string getDisplayName() const override { return "本地设备存储"; }
    std::string getDescription() const override { return "本地磁盘与 MicroSD 卡"; }
    std::string getIconName() const override { return "storage"; }
    bool isNetworkSource() const override { return false; }

    std::vector<MediaItem> listItems(const std::string& path) override {
        return MediaScanner::getInstance().listDirectory(path);
    }

    bool testConnection() override { return true; }
};

StorageProviderRegistry& StorageProviderRegistry::getInstance() {
    static StorageProviderRegistry instance;
    return instance;
}

StorageProviderRegistry::StorageProviderRegistry() {
    // 默认注册基础本地存储
    registerProvider(std::make_shared<LocalStorageProvider>());
}

void StorageProviderRegistry::registerProvider(std::shared_ptr<IStorageProvider> provider) {
    if (!provider) return;
    for (auto& existing : providers_) {
        if (existing->getId() == provider->getId()) {
            existing = provider; // 替换或更新
            return;
        }
    }
    providers_.push_back(provider);
}

std::vector<std::shared_ptr<IStorageProvider>> StorageProviderRegistry::getProviders() const {
    return providers_;
}

std::shared_ptr<IStorageProvider> StorageProviderRegistry::getProvider(const std::string& id) const {
    for (const auto& p : providers_) {
        if (p->getId() == id) return p;
    }
    return nullptr;
}

typedef IStorageProvider* (*CreateProviderFunc)();

void StorageProviderRegistry::loadExternalPlugins(const std::string& custom_dir) {
    std::string plugins_dir = custom_dir.empty() ? AppPaths::pluginsDir() : custom_dir;

    if (!fs::exists(plugins_dir)) return;

    try {
        for (const auto& entry : fs::directory_iterator(plugins_dir)) {
            std::string ext = entry.path().extension().string();
            if (entry.is_regular_file() && (ext == ".so" || ext == ".dylib")) {
                void* handle = dlopen(entry.path().string().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!handle) {
                    std::cerr << "Failed to load MOD plugin: " << dlerror() << std::endl;
                    continue;
                }

                // 寻找插件统一导出符号
                CreateProviderFunc create_fn = (CreateProviderFunc)dlsym(handle, "muvik_create_provider");
                if (create_fn) {
                    IStorageProvider* raw_p = create_fn();
                    if (raw_p) {
                        registerProvider(std::shared_ptr<IStorageProvider>(raw_p));
                        std::cout << "Successfully loaded MOD: " << raw_p->getDisplayName() << std::endl;
                    }
                }
            }
        }
    } catch (...) {}
}

} // namespace muvik
