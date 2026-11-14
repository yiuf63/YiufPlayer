#pragma once

#include "model/network_source.hpp"
#include "model/media_item.hpp"

#include <borealis.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace muvik {

class NetworkBrowserActivity : public brls::Activity {
public:
    explicit NetworkBrowserActivity(NetworkSource source, std::string initial_path = "");
    ~NetworkBrowserActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void render();
    void navigateTo(const std::string& path);
    void navigateUp();
    std::string currentTitle() const;

    NetworkSource source_;
    std::string current_path_;
    std::vector<std::string> path_stack_;

    brls::AppletFrame* frame_ = nullptr;
    brls::Box* list_ = nullptr;
    bool loading_ = false;

    std::vector<std::shared_ptr<std::atomic<bool>>> alive_flags_;
};

} // namespace muvik
