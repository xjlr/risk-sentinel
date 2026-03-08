#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string_view>

namespace sentinel::events::utils {

inline uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return static_cast<uint8_t>(c - 'A' + 10);
  throw std::runtime_error("invalid hex character");
}

constexpr uint8_t const_hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return static_cast<uint8_t>(c - 'A' + 10);
  throw "invalid hex character";
}

template <size_t N>
constexpr std::array<uint8_t, 32> parse_topic_literal(const char (&s)[N]) {
  static_assert(N == 67, "Topic literal must be exactly 66 characters long "
                         "(plus null terminator)");
  if (s[0] != '0' || s[1] != 'x') {
    throw "Topic literal must start with 0x";
  }
  std::array<uint8_t, 32> out{};
  for (size_t i = 0; i < 32; ++i) {
    out[i] = (const_hex_nibble(s[2 + i * 2]) << 4) |
             const_hex_nibble(s[2 + i * 2 + 1]);
  }
  return out;
}

inline uint64_t parse_hex_uint64(std::string_view hex) {
  if (hex.size() < 3 || hex.substr(0, 2) != "0x") {
    throw std::runtime_error("invalid hex uint64");
  }
  uint64_t val = 0;
  for (size_t i = 2; i < hex.size(); ++i) {
    val = (val << 4) | hex_nibble(hex[i]);
  }
  return val;
}

template <size_t N>
inline void parse_hex_bytes(std::string_view hex, std::array<uint8_t, N> &out) {
  if (hex.size() < 2 || hex.substr(0, 2) != "0x") {
    throw std::runtime_error("invalid hex bytes");
  }

  if ((hex.size() - 2) % 2 != 0) {
    throw std::runtime_error("hex string has odd length");
  }

  const size_t hex_len = hex.size() - 2;
  const size_t byte_len = std::min(hex_len / 2, N);

  for (size_t i = 0; i < byte_len; ++i) {
    out[i] = (hex_nibble(hex[2 + i * 2]) << 4) | hex_nibble(hex[2 + i * 2 + 1]);
  }
}

inline void validate_hex(std::string_view hex) {
  if (hex.size() < 2 || hex.substr(0, 2) != "0x") {
    throw std::runtime_error("Expected 0x-prefixed hex string");
  }
  if (((hex.size() - 2) % 2) != 0) {
    throw std::runtime_error("Hex string has odd length");
  }
  for (size_t i = 2; i < hex.size(); ++i) {
    static_cast<void>(hex_nibble(hex[i]));
  }
}

inline std::string to_hex_quantity(uint64_t value) {
  return std::format("{:#x}", value);
}

} // namespace sentinel::events::utils
