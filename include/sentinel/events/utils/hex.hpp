#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <format>

namespace sentinel::events::utils {

inline uint64_t parse_hex_uint64(std::string_view hex) {
    if (hex.size() < 3 || hex.substr(0, 2) != "0x") {
        throw std::runtime_error("invalid hex uint64");
    }
    return std::stoull(std::string(hex.substr(2)), nullptr, 16);
}

template <size_t N>
inline void parse_hex_bytes(
    std::string_view hex,
    std::array<uint8_t, N>& out
) {
    if (hex.size() < 2 || hex.substr(0, 2) != "0x") {
        throw std::runtime_error("invalid hex bytes");
    }

    const size_t hex_len = hex.size() - 2;
    const size_t byte_len = std::min(hex_len / 2, N);

    for (size_t i = 0; i < byte_len; ++i) {
        out[i] = static_cast<uint8_t>(
            std::stoi(std::string(hex.substr(2 + i * 2, 2)), nullptr, 16)
        );
    }
}

inline void validate_hex(std::string_view hex) {
    if (hex.size() < 2 || hex.substr(0,2) != "0x") {
        throw std::runtime_error("Expected 0x-prefixed hex string");
    }
    if (((hex.size() - 2) % 2) != 0) {
        throw std::runtime_error("Hex string has odd length");
    }
}

inline std::string to_hex_quantity(uint64_t value) {
    return std::format("{:#x}", value);
}

} // namespace sentinel::events::utils
