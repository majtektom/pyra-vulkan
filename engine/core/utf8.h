#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace engine3d::utf8 {

// Sprawdza, czy codepoint nalezy do poprawnego zakresu Unicode i nie wpada w surrogate range.
inline bool isValidCodepoint(std::uint32_t codepoint) {
    return codepoint <= 0x10FFFFu && !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

// Dopisuje codepoint tylko raz, aby lista glifow do atlasu nie zawierala duplikatow.
inline void appendUnique(std::vector<std::uint32_t>& codepoints, std::uint32_t codepoint) {
    if (!isValidCodepoint(codepoint)) {
        return;
    }
    if (std::find(codepoints.begin(), codepoints.end(), codepoint) == codepoints.end()) {
        codepoints.push_back(codepoint);
    }
}

// Dekoduje UTF-8 do listy Unicode codepointow; uszkodzone sekwencje zamienia na '?'.
inline std::vector<std::uint32_t> decode(std::string_view text) {
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(text.size());

    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::uint32_t codepoint = 0;
        std::size_t sequenceLength = 0;

        if (lead < 0x80u) {
            codepoint = lead;
            sequenceLength = 1;
        } else if ((lead & 0xE0u) == 0xC0u) {
            codepoint = lead & 0x1Fu;
            sequenceLength = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            codepoint = lead & 0x0Fu;
            sequenceLength = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            codepoint = lead & 0x07u;
            sequenceLength = 4;
        } else {
            codepoints.push_back(static_cast<std::uint32_t>('?'));
            ++index;
            continue;
        }

        if (index + sequenceLength > text.size()) {
            codepoints.push_back(static_cast<std::uint32_t>('?'));
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < sequenceLength; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6u) | static_cast<std::uint32_t>(continuation & 0x3Fu);
        }

        if (!valid || !isValidCodepoint(codepoint)) {
            codepoints.push_back(static_cast<std::uint32_t>('?'));
            ++index;
            continue;
        }

        if ((sequenceLength == 2 && codepoint < 0x80u) ||
            (sequenceLength == 3 && codepoint < 0x800u) ||
            (sequenceLength == 4 && codepoint < 0x10000u)) {
            codepoints.push_back(static_cast<std::uint32_t>('?'));
            ++index;
            continue;
        }

        codepoints.push_back(codepoint);
        index += sequenceLength;
    }

    return codepoints;
}

}  // namespace engine3d::utf8