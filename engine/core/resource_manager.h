#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace engine3d {

template <typename T>
class ResourceManager {
public:
    std::shared_ptr<T> get(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(name);
        if (it != resources_.end()) {
            if (auto shared = it->second.lock()) {
                return shared;
            }
        }
        return nullptr;
    }

    void add(const std::string& name, std::shared_ptr<T> resource) {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_[name] = resource;
    }

    // Load if not present, otherwise return existing
    template <typename Loader>
    std::shared_ptr<T> load(const std::string& name, Loader loader) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = resources_.find(name);
        if (it != resources_.end()) {
            if (auto shared = it->second.lock()) {
                return shared;
            }
        }
        
        auto resource = loader();
        resources_[name] = resource;
        return resource;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        resources_.clear();
    }

private:
    std::unordered_map<std::string, std::weak_ptr<T>> resources_;
    std::mutex mutex_;
};

} // namespace engine3d
