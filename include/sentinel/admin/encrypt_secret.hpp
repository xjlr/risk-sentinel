#pragma once

namespace sentinel::admin {

// Reads SENTINEL_SECRET_MASTER_KEY from env, encrypts the webhook HMAC secret
// (provided via --secret or randomly generated), then prints the SQL INSERT
// statement to stdout.
// Exit code 0 on success, 1 on bad args, 2 on missing/malformed master key,
// 3 on encryption failure.
int encrypt_secret_command(int argc, char **argv);

} // namespace sentinel::admin
