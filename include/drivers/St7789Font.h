#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::uint8_t kSt7789GlyphWidth = 16;
constexpr std::uint8_t kSt7789GlyphHeight = 16;
constexpr std::size_t kSt7789GlyphBytes = 32;

struct St7789Glyph {
    std::uint32_t codepoint;
    std::uint8_t bitmap[kSt7789GlyphBytes];
};

extern const St7789Glyph kSt7789Glyphs[];
extern const std::size_t kSt7789GlyphCount;

const St7789Glyph* findSt7789Glyph(std::uint32_t codepoint);

}  // namespace faucet
