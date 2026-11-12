#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace muvik {

// 插件配置存储。
//
// 每个插件按 id 拥有独立的键值对配置，持久化到
// ~/.config/muvik/plugin_config.dat。
//
// 用途：视频源插件（如 WebDAV）需要服务器地址与账号密码，
// 这些必须能被用户在界面上填写并保存，而不是只能靠环境变量。
class PluginConfigStore {
public:
    static PluginConfigStore& getInstance();

    // 读取某个插件的某项配置，不存在时返回空串
    std::string get(const std::string& plugin_id, const std::string& key) const;

    // 设置配置项（空值表示删除）
    void set(const std::string& plugin_id, const std::string& key,
             const std::string& value);

    // 获取某个插件的全部配置
    std::map<std::string, std::string> getAll(const std::string& plugin_id) const;

    // 是否已有该插件的任何配置
    bool hasConfig(const std::string& plugin_id) const;

    // 清除某个插件的全部配置
    void clear(const std::string& plugin_id);

    // 把所有配置导出为 JSON（供传递给插件的 config_json）
    std::string toJson(const std::string& plugin_id) const;

    // 持久化
    void load();
    void save() const;

private:
    PluginConfigStore();
    std::string filePath() const;

    mutable std::recursive_mutex mutex_;
    // plugin_id -> (key -> value)
    std::map<std::string, std::map<std::string, std::string>> data_;
};

} // namespace muvik
