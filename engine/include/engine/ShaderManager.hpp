#pragma once

#include <string>
#include <unordered_map>

#include "engine/Types.hpp"

namespace engine3d {

// Odpowiada za szybkie wyszukiwanie programu shaderowego po nazwie z manifestu.
class ShaderManager {
public:
    // Buduje wewnetrzna mape nazw shaderow podczas startu silnika.
    explicit ShaderManager(const ShaderManifest& manifest);

    // Zwraca zadany program albo rzuca wyjatek, gdy nazwa nie istnieje.
    [[nodiscard]] const ShaderProgram& require(const std::string& name) const;

private:
    // Mapa pozwala pobrac konfiguracje programu w czasie stalym.
    std::unordered_map<std::string, ShaderProgram> programs_;
};

}  // namespace engine3d