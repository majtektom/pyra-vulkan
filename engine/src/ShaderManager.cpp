#include "engine/ShaderManager.hpp"

#include <stdexcept>

namespace engine3d {

// Przepisuje manifest do mapy, aby dalsze pobieranie shaderow bylo proste i szybkie.
ShaderManager::ShaderManager(const ShaderManifest& manifest) {
    for (const auto& entry : manifest.entries) {
        if (entry.name.empty()) {
            throw std::runtime_error("shader manifest contains an empty program name");
        }

        // Kazdy wpis zapisujemy pod nazwa logiczna uzywana potem przez renderer.
        programs_.emplace(entry.name, entry.program);
    }
}

// Zwraca shader albo zglasza czytelny blad, jesli manifest nie zawiera zadanej nazwy.
const ShaderProgram& ShaderManager::require(const std::string& name) const {
    const auto iterator = programs_.find(name);
    if (iterator == programs_.end()) {
        throw std::runtime_error("shader program not found in manifest: " + name);
    }
    return iterator->second;
}

}  // namespace engine3d