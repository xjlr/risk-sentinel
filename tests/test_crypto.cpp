#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "sentinel/security/crypto.hpp"

using namespace sentinel::security;

// ---------------------------------------------------------------------------
// HMAC-SHA256
// ---------------------------------------------------------------------------

TEST_CASE("hmac_sha256_hex known vector", "[crypto][hmac]") {
    // Widely-published canonical HMAC-SHA256 test vector
    const std::string digest =
        hmac_sha256_hex("key", "The quick brown fox jumps over the lazy dog");
    REQUIRE(digest ==
            "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST_CASE("hmac_sha256_hex empty data", "[crypto][hmac]") {
    // Must not throw; result is deterministic for a given key
    const std::string a = hmac_sha256_hex("somekey", "");
    const std::string b = hmac_sha256_hex("somekey", "");
    REQUIRE(a == b);
    REQUIRE(a.size() == 64); // 32 bytes → 64 hex chars
}

// ---------------------------------------------------------------------------
// parse_master_key_hex
// ---------------------------------------------------------------------------

TEST_CASE("parse_master_key_hex valid 64-char hex", "[crypto][parse]") {
    const std::string hex(64, 'a'); // "aaaa...aa"
    auto key = parse_master_key_hex(hex);
    REQUIRE(key.size() == 32);
    for (auto b : key)
        REQUIRE(b == 0xaa);
}

TEST_CASE("parse_master_key_hex mixed case is accepted", "[crypto][parse]") {
    // 32 bytes of 0xAB alternating
    std::string hex;
    hex.reserve(64);
    for (int i = 0; i < 32; ++i)
        hex += (i % 2 == 0) ? "AB" : "ab";
    auto key = parse_master_key_hex(hex);
    for (auto b : key)
        REQUIRE(b == 0xab);
}

TEST_CASE("parse_master_key_hex 63-char input throws", "[crypto][parse]") {
    const std::string hex(63, '0');
    REQUIRE_THROWS_AS(parse_master_key_hex(hex), std::runtime_error);
}

TEST_CASE("parse_master_key_hex 65-char input throws", "[crypto][parse]") {
    const std::string hex(65, '0');
    REQUIRE_THROWS_AS(parse_master_key_hex(hex), std::runtime_error);
}

TEST_CASE("parse_master_key_hex non-hex chars throw", "[crypto][parse]") {
    std::string hex(64, '0');
    hex[10] = 'z'; // invalid hex character
    REQUIRE_THROWS_AS(parse_master_key_hex(hex), std::runtime_error);
}

// ---------------------------------------------------------------------------
// AES-256-GCM round-trip
// ---------------------------------------------------------------------------

static std::array<uint8_t, 32> make_test_key() {
    std::array<uint8_t, 32> k{};
    for (std::size_t i = 0; i < 32; ++i)
        k[i] = static_cast<uint8_t>(i + 1);
    return k;
}

static std::array<uint8_t, 12> make_test_nonce() {
    std::array<uint8_t, 12> n{};
    for (std::size_t i = 0; i < 12; ++i)
        n[i] = static_cast<uint8_t>(0x10 + i);
    return n;
}

TEST_CASE("AES-256-GCM encrypt then decrypt round-trip", "[crypto][aes]") {
    auto key   = make_test_key();
    auto nonce = make_test_nonce();
    const std::string plaintext = "Hello, Risk Sentinel!";

    const std::span<const uint8_t> key_span(key.data(), key.size());
    const std::span<const uint8_t> nonce_span(nonce.data(), nonce.size());

    const std::vector<uint8_t> ciphertext =
        aes_gcm_encrypt(key_span, nonce_span, plaintext);

    // ciphertext must be longer than plaintext (appends 16-byte auth tag)
    REQUIRE(ciphertext.size() == plaintext.size() + 16);

    const std::string recovered =
        aes_gcm_decrypt(key_span, nonce_span,
                        std::span<const uint8_t>(ciphertext.data(),
                                                  ciphertext.size()));
    REQUIRE(recovered == plaintext);
}

TEST_CASE("AES-256-GCM empty plaintext round-trip", "[crypto][aes]") {
    auto key   = make_test_key();
    auto nonce = make_test_nonce();

    const std::span<const uint8_t> key_span(key.data(), key.size());
    const std::span<const uint8_t> nonce_span(nonce.data(), nonce.size());

    const std::vector<uint8_t> ciphertext =
        aes_gcm_encrypt(key_span, nonce_span, "");

    // Only the 16-byte auth tag
    REQUIRE(ciphertext.size() == 16);

    const std::string recovered =
        aes_gcm_decrypt(key_span, nonce_span,
                        std::span<const uint8_t>(ciphertext.data(),
                                                  ciphertext.size()));
    REQUIRE(recovered.empty());
}

TEST_CASE("AES-256-GCM ciphertext tampering throws", "[crypto][aes]") {
    auto key   = make_test_key();
    auto nonce = make_test_nonce();
    const std::string plaintext = "tamper test payload";

    const std::span<const uint8_t> key_span(key.data(), key.size());
    const std::span<const uint8_t> nonce_span(nonce.data(), nonce.size());

    std::vector<uint8_t> ciphertext =
        aes_gcm_encrypt(key_span, nonce_span, plaintext);

    // Flip one bit in the encrypted body (not the tag at the end)
    ciphertext[0] ^= 0x01;

    REQUIRE_THROWS_AS(
        aes_gcm_decrypt(key_span, nonce_span,
                        std::span<const uint8_t>(ciphertext.data(),
                                                  ciphertext.size())),
        std::runtime_error);
}

TEST_CASE("AES-256-GCM auth-tag tampering throws", "[crypto][aes]") {
    auto key   = make_test_key();
    auto nonce = make_test_nonce();
    const std::string plaintext = "tag tamper payload";

    const std::span<const uint8_t> key_span(key.data(), key.size());
    const std::span<const uint8_t> nonce_span(nonce.data(), nonce.size());

    std::vector<uint8_t> ciphertext =
        aes_gcm_encrypt(key_span, nonce_span, plaintext);

    // Flip one bit in the auth tag (last byte)
    ciphertext.back() ^= 0x01;

    REQUIRE_THROWS_AS(
        aes_gcm_decrypt(key_span, nonce_span,
                        std::span<const uint8_t>(ciphertext.data(),
                                                  ciphertext.size())),
        std::runtime_error);
}

TEST_CASE("AES-256-GCM wrong key throws on decrypt", "[crypto][aes]") {
    auto key   = make_test_key();
    auto nonce = make_test_nonce();

    const std::span<const uint8_t> key_span(key.data(), key.size());
    const std::span<const uint8_t> nonce_span(nonce.data(), nonce.size());

    const std::vector<uint8_t> ciphertext =
        aes_gcm_encrypt(key_span, nonce_span, "secret");

    // Use a different key for decryption
    auto wrong_key = key;
    wrong_key[0] ^= 0xff;
    const std::span<const uint8_t> wrong_key_span(wrong_key.data(),
                                                   wrong_key.size());

    REQUIRE_THROWS_AS(
        aes_gcm_decrypt(wrong_key_span, nonce_span,
                        std::span<const uint8_t>(ciphertext.data(),
                                                  ciphertext.size())),
        std::runtime_error);
}
