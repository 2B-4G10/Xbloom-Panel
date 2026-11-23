// Golden test for cloud_crypto: with a fixed "random" source the encryption is
// repeatable, so two versions of cloud_crypto.cpp must print the same lines.
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "cloud_crypto.h"

static unsigned seed;
static int fixed_rng(void *, unsigned char *out, size_t n) {
  for (size_t i = 0; i < n; ++i) out[i] = (unsigned char)((seed = seed * 1103515245u + 12345u) % 255 + 1);
  return 0;
}

int main() {
  for (int len : {0, 1, 116, 117, 118, 234, 235, 500, 1000, 1500}) {
    std::string plain;
    for (int i = 0; i < len; ++i) plain += (char)('a' + i % 26);
    if (len > 50) plain += "قهوة";
    seed = (unsigned)len;
    std::string out;
    const bool ok = cloudcrypto::encrypt_body(cloudcrypto::XBLOOM_PUBLIC_KEY_PEM, plain, out, fixed_rng, nullptr);
    printf("%d %d %s\n", len, ok, out.c_str());
  }
  std::string out;
  printf("bad %d\n", cloudcrypto::encrypt_body("not a key", "x", out, fixed_rng, nullptr));
  return 0;
}
