#include "sentinel/admin/encrypt_secret.hpp"
#include "sentinel/security/crypto.hpp"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <openssl/rand.h>

namespace sentinel::admin {

namespace {

std::string bytes_to_hex(const uint8_t *data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    return oss.str();
}

std::string generate_random_hex64() {
    uint8_t buf[32];
    if (RAND_bytes(buf, static_cast<int>(sizeof(buf))) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return bytes_to_hex(buf, sizeof(buf));
}

void print_usage(const char *prog) {
    std::cerr
        << "Usage: " << prog
        << " admin encrypt-secret"
           " --customer-id <uint64>"
           " --url <url>"
           " [--secret <plaintext>]\n"
           "\n"
           "  Reads SENTINEL_SECRET_MASTER_KEY from env.\n"
           "  If --secret is omitted, a random 64-char hex secret is "
           "generated.\n";
}

} // namespace

int encrypt_secret_command(int argc, char **argv) {
    uint64_t customer_id = 0;
    bool has_customer_id = false;
    std::string url;
    std::string secret_plaintext;

    // argv: [sentinel, admin, encrypt-secret, ...]
    for (int i = 3; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--customer-id" && i + 1 < argc) {
            try {
                customer_id = std::stoull(argv[++i]);
                has_customer_id = true;
            } catch (...) {
                std::cerr << "Error: --customer-id must be a valid uint64\n";
                return 1;
            }
        } else if (arg == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (arg == "--secret" && i + 1 < argc) {
            secret_plaintext = argv[++i];
        } else {
            std::cerr << "Error: unknown argument '" << arg << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!has_customer_id || url.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    const char *master_key_env =
        std::getenv("SENTINEL_SECRET_MASTER_KEY");
    if (!master_key_env || std::strlen(master_key_env) == 0) {
        std::cerr << "Error: SENTINEL_SECRET_MASTER_KEY is not set or empty\n";
        return 2;
    }

    std::array<uint8_t, 32> master_key{};
    try {
        master_key =
            sentinel::security::parse_master_key_hex(master_key_env);
    } catch (const std::exception &e) {
        std::cerr << "Error: invalid SENTINEL_SECRET_MASTER_KEY: " << e.what()
                  << "\n";
        return 2;
    }

    if (secret_plaintext.empty()) {
        try {
            secret_plaintext = generate_random_hex64();
        } catch (const std::exception &e) {
            std::cerr << "Error: failed to generate random secret: " << e.what()
                      << "\n";
            return 3;
        }
    }

    // Generate a fresh 12-byte nonce
    std::array<uint8_t, 12> nonce_bytes{};
    if (RAND_bytes(nonce_bytes.data(),
                   static_cast<int>(nonce_bytes.size())) != 1) {
        std::cerr << "Error: RAND_bytes failed for nonce generation\n";
        return 3;
    }

    std::vector<uint8_t> ciphertext;
    try {
        ciphertext = sentinel::security::aes_gcm_encrypt(
            std::span<const uint8_t>(master_key.data(), master_key.size()),
            std::span<const uint8_t>(nonce_bytes.data(), nonce_bytes.size()),
            secret_plaintext);
    } catch (const std::exception &e) {
        std::cerr << "Error: encryption failed: " << e.what() << "\n";
        return 3;
    }

    const std::string nonce_hex =
        bytes_to_hex(nonce_bytes.data(), nonce_bytes.size());
    const std::string ciphertext_hex =
        bytes_to_hex(ciphertext.data(), ciphertext.size());

    std::cout
        << "\n=== Risk Sentinel — webhook secret provisioned ===\n"
        << "\n"
        << "Customer ID: " << customer_id << "\n"
        << "URL:         " << url << "\n"
        << "\n"
        << "Plaintext secret (share with customer ONCE, never store):\n"
        << "    " << secret_plaintext << "\n"
        << "\n"
        << "SQL to run:\n"
        << "    INSERT INTO customer_webhook_channels\n"
        << "        (customer_id, url, hmac_secret_encrypted, "
           "hmac_secret_nonce, enabled)\n"
        << "    VALUES (\n"
        << "        " << customer_id << ",\n"
        << "        '" << url << "',\n"
        << "        decode('" << ciphertext_hex << "', 'hex'),\n"
        << "        decode('" << nonce_hex << "', 'hex'),\n"
        << "        true\n"
        << "    );\n"
        << "\n";

    return 0;
}

} // namespace sentinel::admin
