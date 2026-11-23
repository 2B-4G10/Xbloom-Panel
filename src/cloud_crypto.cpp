// xBloom account API body encryption. See cloud_crypto.h.
#include "cloud_crypto.h"

#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace cloudcrypto {

// xBloom's public key: the same 1024-bit RSA key in every copy of their app.
const char XBLOOM_PUBLIC_KEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC4LF40GZ72SdhMyl765K/i4nY5\n"
    "CPcHz2Q1IKWKZ9S79xmK7G8pUhbVf4EZLvnNF1+9IvOFQUKV5Z7ZNNviqSpnql9t\n"
    "AT+8+J/He0R7pcirvVSxgdr2i9V/C/gmqAEZ5qVTzRnd3uWdFoKzPdEBxP0IporJ\n"
    "1VBbCv90yBSOhVxO+QIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

namespace {

// Most plaintext one PKCS#1 v1.5 block can carry with a 1024-bit key: 128 - 11.
constexpr size_t PLAIN_PER_BLOCK = 117;

// Owns an mbedTLS key for the length of one call.
struct PublicKey {
  mbedtls_pk_context ctx;
  PublicKey() { mbedtls_pk_init(&ctx); }
  ~PublicKey() { mbedtls_pk_free(&ctx); }
  bool load(const char *pem) {
    return mbedtls_pk_parse_public_key(&ctx, (const unsigned char *)pem, strlen(pem) + 1) == 0;
  }
};

bool to_base64(const std::vector<unsigned char> &bin, std::string &out) {
  size_t need = 0;
  mbedtls_base64_encode(nullptr, 0, &need, bin.data(), bin.size());  // asks for the size
  out.assign(need, '\0');
  size_t written = 0;
  if (mbedtls_base64_encode((unsigned char *)&out[0], need, &written, bin.data(), bin.size()) != 0) return false;
  out.resize(written);
  return true;
}

}  // namespace

bool encrypt_body(const char *key_pem, const std::string &plain, std::string &out_b64, Rng rng, void *rng_ctx) {
  PublicKey key;
  if (!key.load(key_pem)) return false;
  const size_t block = mbedtls_pk_get_len(&key.ctx);
  std::vector<unsigned char> cipher;
  for (size_t at = 0; at < plain.size(); at += PLAIN_PER_BLOCK) {
    const size_t n = std::min(PLAIN_PER_BLOCK, plain.size() - at);
    cipher.resize(cipher.size() + block);
    size_t produced = 0;
    if (mbedtls_pk_encrypt(&key.ctx, (const unsigned char *)plain.data() + at, n, cipher.data() + cipher.size() - block,
                           &produced, block, rng, rng_ctx) != 0 ||
        produced != block)
      return false;
  }
  return to_base64(cipher, out_b64);
}

}  // namespace cloudcrypto
