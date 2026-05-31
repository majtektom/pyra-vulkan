#pragma once

#include <string>
#include <unordered_map>

#include "core/types.h"

namespace engine3d {

class MaterialManager {
public:
    MaterialManager() = default;

    void add(const Material& material);

    [[nodiscard]] const Material* get(const std::string& name) const;
    const Material& require(const std::string& name) const;

    bool has(const std::string& name) const;

private:
    std::unordered_map<std::string, Material> materials_;
};

}  // namespace engine3d
