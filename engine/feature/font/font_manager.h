#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace engine3d {

class FontManager {
public:
    FontManager() = default;
    explicit FontManager(const std::vector<FontAsset>& fonts);

    void add(const std::string& name, const std::string& path, uint32_t pixelHeight = 48);
    void add(const FontAsset& font);
    [[nodiscard]] const FontAsset* get(const std::string& name) const;
    [[nodiscard]] const FontAsset& require(const std::string& name) const;

private:
    std::unordered_map<std::string, FontAsset> fonts_;
};

} // namespace engine3d
