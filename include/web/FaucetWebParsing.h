#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

bool parseU32(const char* text, std::uint32_t& value);
bool parseDate(const char* text, std::uint32_t& seconds);
void formatDate(std::uint32_t seconds, char* out, std::size_t len);
bool parseFloat(const char* text, float& value);

}  // namespace faucet
