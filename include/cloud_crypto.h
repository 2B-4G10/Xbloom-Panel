// Encryption of request bodies for the xBloom account API, done the way their app
// does it: the JSON is cut into 117-byte pieces, each RSA-encrypted (PKCS#1 v1.5)
// with xBloom's public key, joined and base64-encoded. mbedTLS only; tested on a PC
// (test_native/crypto_golden.cpp).
#pragma once
#include <stddef.h>
#include <string>

namespace cloudcrypto {

extern const char XBLOOM_PUBLIC_KEY_PEM[];

using Rng = int (*)(void *ctx, unsigned char *out, size_t len);

// key_pem must be NUL-terminated. Returns false on any mbedTLS error.
bool encrypt_body(const char *key_pem, const std::string &plain, std::string &out_b64, Rng rng, void *rng_ctx);

}  // namespace cloudcrypto
