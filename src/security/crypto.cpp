#include "sentinel/security/crypto.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/params.h>

namespace sentinel::security {

namespace {

struct EvpCipherCtxGuard {
    EVP_CIPHER_CTX *ctx;
    explicit EvpCipherCtxGuard(EVP_CIPHER_CTX *c) : ctx(c) {}
    ~EvpCipherCtxGuard() {
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
    }
    EvpCipherCtxGuard(const EvpCipherCtxGuard &) = delete;
    EvpCipherCtxGuard &operator=(const EvpCipherCtxGuard &) = delete;
};

std::string bytes_to_hex(const uint8_t *data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

} // namespace

std::string aes_gcm_decrypt(std::span<const uint8_t> master_key,
                             std::span<const uint8_t> nonce,
                             std::span<const uint8_t> ciphertext) {
    constexpr size_t TAG_LEN = 16;

    if (master_key.size() != 32)
        throw std::runtime_error("aes_gcm_decrypt: master_key must be 32 bytes");
    if (nonce.size() != 12)
        throw std::runtime_error("aes_gcm_decrypt: nonce must be 12 bytes");
    if (ciphertext.size() < TAG_LEN)
        throw std::runtime_error("aes_gcm_decrypt: ciphertext too short (< 16 bytes)");

    const size_t enc_len = ciphertext.size() - TAG_LEN;

    // Extract auth tag — needs a mutable buffer for EVP_CIPHER_CTX_ctrl
    std::vector<uint8_t> tag(ciphertext.end() - TAG_LEN, ciphertext.end());

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throw std::runtime_error("aes_gcm_decrypt: EVP_CIPHER_CTX_new failed");
    EvpCipherCtxGuard guard(ctx);

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        throw std::runtime_error("aes_gcm_decrypt: EVP_DecryptInit_ex (cipher) failed");

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1)
        throw std::runtime_error("aes_gcm_decrypt: setting IV length failed");

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, master_key.data(),
                           nonce.data()) != 1)
        throw std::runtime_error("aes_gcm_decrypt: EVP_DecryptInit_ex (key/iv) failed");

    std::string plaintext(enc_len, '\0');
    int out_len = 0;

    if (enc_len > 0) {
        if (EVP_DecryptUpdate(ctx,
                              reinterpret_cast<uint8_t *>(plaintext.data()),
                              &out_len, ciphertext.data(),
                              static_cast<int>(enc_len)) != 1)
            throw std::runtime_error("aes_gcm_decrypt: EVP_DecryptUpdate failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                             static_cast<int>(TAG_LEN), tag.data()) != 1)
        throw std::runtime_error("aes_gcm_decrypt: setting GCM tag failed");

    int final_len = 0;
    if (EVP_DecryptFinal_ex(
            ctx,
            reinterpret_cast<uint8_t *>(plaintext.data()) + out_len,
            &final_len) <= 0)
        throw std::runtime_error(
            "aes_gcm_decrypt: authentication tag verification failed");

    plaintext.resize(static_cast<size_t>(out_len + final_len));
    return plaintext;
}

std::vector<uint8_t> aes_gcm_encrypt(std::span<const uint8_t> master_key,
                                      std::span<const uint8_t> nonce,
                                      std::string_view plaintext) {
    constexpr size_t TAG_LEN = 16;

    if (master_key.size() != 32)
        throw std::runtime_error("aes_gcm_encrypt: master_key must be 32 bytes");
    if (nonce.size() != 12)
        throw std::runtime_error("aes_gcm_encrypt: nonce must be 12 bytes");

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throw std::runtime_error("aes_gcm_encrypt: EVP_CIPHER_CTX_new failed");
    EvpCipherCtxGuard guard(ctx);

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        throw std::runtime_error("aes_gcm_encrypt: EVP_EncryptInit_ex (cipher) failed");

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1)
        throw std::runtime_error("aes_gcm_encrypt: setting IV length failed");

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, master_key.data(),
                           nonce.data()) != 1)
        throw std::runtime_error("aes_gcm_encrypt: EVP_EncryptInit_ex (key/iv) failed");

    // Allocate space for ciphertext + tag
    std::vector<uint8_t> output(plaintext.size() + TAG_LEN);
    int out_len = 0;

    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(
                ctx, output.data(), &out_len,
                reinterpret_cast<const uint8_t *>(plaintext.data()),
                static_cast<int>(plaintext.size())) != 1)
            throw std::runtime_error("aes_gcm_encrypt: EVP_EncryptUpdate failed");
    }

    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx, output.data() + out_len, &final_len) != 1)
        throw std::runtime_error("aes_gcm_encrypt: EVP_EncryptFinal_ex failed");

    // Append 16-byte auth tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                             static_cast<int>(TAG_LEN),
                             output.data() + out_len + final_len) != 1)
        throw std::runtime_error("aes_gcm_encrypt: getting GCM tag failed");

    output.resize(static_cast<size_t>(out_len + final_len) + TAG_LEN);
    return output;
}

std::string hmac_sha256_hex(std::string_view key, std::string_view data) {
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac)
        throw std::runtime_error("hmac_sha256_hex: EVP_MAC_fetch failed");

    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!ctx)
        throw std::runtime_error("hmac_sha256_hex: EVP_MAC_CTX_new failed");

    // OSSL_PARAM array specifying SHA-256 as the digest
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest",
                                         const_cast<char *>("SHA256"), 0),
        OSSL_PARAM_END};

    if (EVP_MAC_init(ctx, reinterpret_cast<const uint8_t *>(key.data()),
                     key.size(), params) != 1) {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("hmac_sha256_hex: EVP_MAC_init failed");
    }

    if (EVP_MAC_update(ctx, reinterpret_cast<const uint8_t *>(data.data()),
                       data.size()) != 1) {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("hmac_sha256_hex: EVP_MAC_update failed");
    }

    uint8_t digest[32];
    size_t digest_len = sizeof(digest);
    if (EVP_MAC_final(ctx, digest, &digest_len, sizeof(digest)) != 1) {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("hmac_sha256_hex: EVP_MAC_final failed");
    }

    EVP_MAC_CTX_free(ctx);
    return bytes_to_hex(digest, digest_len);
}

std::array<uint8_t, 32> parse_master_key_hex(std::string_view hex) {
    if (hex.size() != 64)
        throw std::runtime_error(
            "parse_master_key_hex: must be exactly 64 hex characters, got " +
            std::to_string(hex.size()));

    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f')
            return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return static_cast<uint8_t>(c - 'A' + 10);
        throw std::runtime_error(
            std::string("parse_master_key_hex: invalid hex character '") + c +
            "'");
    };

    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<uint8_t>((nibble(hex[2 * i]) << 4) |
                                       nibble(hex[2 * i + 1]));
    }
    return key;
}

} // namespace sentinel::security
