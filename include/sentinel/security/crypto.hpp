#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::security {

struct EncryptedSecret {
    std::vector<uint8_t> ciphertext;
    std::array<uint8_t, 12> nonce;
};

// AES-256-GCM decryption using OpenSSL EVP_* API.
// GCM auth tag must be the last 16 bytes of ciphertext.
// Throws std::runtime_error on authentication failure or malformed input.
// master_key must be exactly 32 bytes, nonce must be exactly 12 bytes.
std::string aes_gcm_decrypt(
    std::span<const uint8_t> master_key,
    std::span<const uint8_t> nonce,
    std::span<const uint8_t> ciphertext
);

// AES-256-GCM encryption using OpenSSL EVP_* API.
// Returns ciphertext bytes with the 16-byte auth tag appended.
// master_key must be exactly 32 bytes, nonce must be exactly 12 bytes.
std::vector<uint8_t> aes_gcm_encrypt(
    std::span<const uint8_t> master_key,
    std::span<const uint8_t> nonce,
    std::string_view plaintext
);

// HMAC-SHA256 returning lowercase hex digest.
// Uses OpenSSL EVP_MAC API (not the deprecated HMAC() function).
std::string hmac_sha256_hex(std::string_view key, std::string_view data);

// Parses a 64-char hex string into 32 bytes. Throws on invalid input.
std::array<uint8_t, 32> parse_master_key_hex(std::string_view hex);

} // namespace sentinel::security
