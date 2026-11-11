#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "platform/platform.h"

namespace engine3d {

using InputActionName = std::string;

class InputActionMap {
public:
    void clearAll() {
        bindings_.clear();
    }

    void bindKey(std::string_view action, int scancode) {
        if (scancode < 0 || scancode > 255) {
            return;
        }
        auto& keys = bindings_[std::string(action)];
        const auto code = static_cast<uint8_t>(scancode);
        if (std::find(keys.begin(), keys.end(), code) == keys.end()) {
            keys.push_back(code);
        }
    }

    bool isDown(const InputSnapshot& snapshot, std::string_view action) const {
        const auto it = bindings_.find(std::string(action));
        if (it == bindings_.end()) {
            return false;
        }
        return anyKeyDown(snapshot, it->second);
    }

    bool wasPressed(const InputSnapshot& previous, const InputSnapshot& current, std::string_view action) const {
        return !isDown(previous, action) && isDown(current, action);
    }

private:
    static bool anyKeyDown(const InputSnapshot& snapshot, const std::vector<uint8_t>& keys) {
        for (uint8_t key : keys) {
            if (snapshot.keys[key]) {
                return true;
            }
        }
        return false;
    }

    std::unordered_map<InputActionName, std::vector<uint8_t>> bindings_;
};

} // namespace engine3d