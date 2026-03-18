#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
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

inline std::array<uint8_t, 32> to_be_256(uint64_t value) {
  std::array<uint8_t, 32> result{};
  for (int i = 0; i < 8; ++i) {
    result[31 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
  }
  return result;
}

// Compares two 32-byte big-endian values. Returns true if lhs > rhs.
inline bool greater_be_256(const uint8_t *lhs, const uint8_t *rhs) {
  return std::memcmp(lhs, rhs, 32) > 0;
}

// Converts a 32-byte big-endian value to a base-10 string
inline std::string uint256_be_to_decimal(const uint8_t *data) {
  std::array<uint8_t, 32> buffer;
  std::memcpy(buffer.data(), data, 32);

  bool is_zero = true;
  for (int i = 0; i < 32; ++i) {
    if (buffer[i] != 0) {
      is_zero = false;
      break;
    }
  }

  if (is_zero) {
    return "0";
  }

  std::string result;
  is_zero = false;
  while (!is_zero) {
    uint32_t remainder = 0;
    is_zero = true;
    for (int i = 0; i < 32; ++i) {
      uint32_t num = (remainder << 8) | buffer[i];
      buffer[i] = static_cast<uint8_t>(num / 10);
      remainder = num % 10;
      if (buffer[i] != 0) {
        is_zero = false;
      }
    }
    result.push_back(static_cast<char>('0' + remainder));
  }

  std::reverse(result.begin(), result.end());
  return result;
}

// Converts a base-10 string (decimal) to a 32-byte big-endian value.
inline std::array<uint8_t, 32> decimal_to_be_256(std::string_view decimal) {
  std::array<uint8_t, 32> result{};
  for (char c : decimal) {
    if (c < '0' || c > '9') {
      throw std::runtime_error("invalid decimal digit");
    }
    uint32_t digit = c - '0';

    // result = result * 10 + digit
    uint32_t carry = digit;
    for (int i = 31; i >= 0; --i) {
      uint32_t val = (static_cast<uint32_t>(result[i]) * 10) + carry;
      result[i] = static_cast<uint8_t>(val & 0xFF);
      carry = val >> 8;
    }
    if (carry > 0) {
      throw std::runtime_error("decimal string too large for 256-bit int");
    }
  }
  return result;
}

} // namespace sentinel::events::utils
