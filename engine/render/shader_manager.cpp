#include <stdexcept>

#include "render/shader_manager.h"

namespace engine3d {

ShaderManager::ShaderManager(const ShaderManifest& manifest) {
    for (const auto& entry : manifest.entries) {
        add(entry.name, entry.program);
    }
}

void ShaderManager::add(const std::string& name, const ShaderProgram& program) {
    shaders_[name] = program;
}

const ShaderProgram* ShaderManager::get(const std::string& name) const {
    auto it = shaders_.find(name);
    return it != shaders_.end() ? &it->second : nullptr;
}

const ShaderProgram& ShaderManager::require(const std::string& name) const {
    auto it = shaders_.find(name);
    if (it == shaders_.end()) {
        throw std::runtime_error("shader not found: " + name);
    }
    return it->second;
}

} // namespace engine3d
