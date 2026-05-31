#pragma once

#include <string>
#include <unordered_map>

#include "core/types.h"

namespace engine3d {

class ShaderManager {
public:
    ShaderManager() = default;
    explicit ShaderManager(const ShaderManifest& manifest);

    void add(const std::string& name, const ShaderProgram& program);
    [[nodiscard]] const ShaderProgram* get(const std::string& name) const;
    [[nodiscard]] const ShaderProgram& require(const std::string& name) const;

private:
    std::unordered_map<std::string, ShaderProgram> shaders_;
};

} // namespace engine3d
