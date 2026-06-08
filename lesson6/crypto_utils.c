/**
 * @file crypto_utils.c
 * @brief 数据安全工具模块实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现轻量级数据安全功能，适合嵌入式ARM平台。
 * 不依赖外部加密库，使用纯C实现。
 */

#include "crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              SHA-256实现 */
/* ========================================================================== */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa704,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64]) {
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t t1, t2;
  int i;

  for (i = 0; i < 16; i++) {
    w[i] = read_be32(block + i * 4);
  }
  for (i = 16; i < 64; i++) {
    w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64; i++) {
    t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
    t2 = EP0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

crypto_error_t sha256_init(sha256_ctx_t *ctx) {
  if (!ctx) return CRYPTO_ERROR_PARAM;

  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  ctx->count = 0;
  memset(ctx->buffer, 0, 64);

  return CRYPTO_OK;
}

crypto_error_t sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
  if (!ctx || !data) return CRYPTO_ERROR_PARAM;

  const uint8_t *p = (const uint8_t *)data;
  size_t buffered = (size_t)(ctx->count / 8) % 64;

  ctx->count += (uint64_t)len * 8;

  if (buffered > 0) {
    size_t fill = 64 - buffered;
    if (len >= fill) {
      memcpy(ctx->buffer + buffered, p, fill);
      sha256_transform(ctx, ctx->buffer);
      p += fill;
      len -= fill;
      buffered = 0;
    } else {
      memcpy(ctx->buffer + buffered, p, len);
      return CRYPTO_OK;
    }
  }

  while (len >= 64) {
    sha256_transform(ctx, p);
    p += 64;
    len -= 64;
  }

  if (len > 0) {
    memcpy(ctx->buffer, p, len);
  }

  return CRYPTO_OK;
}

crypto_error_t sha256_final(sha256_ctx_t *ctx, uint8_t hash[SHA256_HASH_SIZE]) {
  if (!ctx || !hash) return CRYPTO_ERROR_PARAM;

  size_t buffered = (size_t)(ctx->count / 8) % 64;
  ctx->buffer[buffered++] = 0x80;

  if (buffered > 56) {
    memset(ctx->buffer + buffered, 0, 64 - buffered);
    sha256_transform(ctx, ctx->buffer);
    buffered = 0;
  }

  memset(ctx->buffer + buffered, 0, 56 - buffered);
  write_be32(ctx->buffer + 56, (uint32_t)(ctx->count >> 32));
  write_be32(ctx->buffer + 60, (uint32_t)ctx->count);
  sha256_transform(ctx, ctx->buffer);

  for (int i = 0; i < 8; i++) {
    write_be32(hash + i * 4, ctx->state[i]);
  }

  secure_memzero(ctx, sizeof(sha256_ctx_t));
  return CRYPTO_OK;
}

crypto_error_t sha256_calc(const void *data, size_t len, uint8_t hash[SHA256_HASH_SIZE]) {
  sha256_ctx_t ctx;
  crypto_error_t ret;

  ret = sha256_init(&ctx);
  if (ret != CRYPTO_OK) return ret;

  ret = sha256_update(&ctx, data, len);
  if (ret != CRYPTO_OK) return ret;

  return sha256_final(&ctx, hash);
}

crypto_error_t sha256_hex(const void *data, size_t len, char hex_out[SHA256_HEX_SIZE]) {
  uint8_t hash[SHA256_HASH_SIZE];
  crypto_error_t ret = sha256_calc(data, len, hash);
  if (ret != CRYPTO_OK) return ret;

  for (int i = 0; i < SHA256_HASH_SIZE; i++) {
    sprintf(hex_out + i * 2, "%02x", hash[i]);
  }
  hex_out[64] = '\0';

  secure_memzero(hash, sizeof(hash));
  return CRYPTO_OK;
}

/* ========================================================================== */
/*                              XOR加密实现 */
/* ========================================================================== */

crypto_error_t xor_init(xor_ctx_t *ctx, const void *key, size_t key_len) {
  if (!ctx || !key || key_len == 0 || key_len > CRYPTO_MAX_KEY_SIZE) {
    return CRYPTO_ERROR_PARAM;
  }

  memcpy(ctx->key, key, key_len);
  ctx->key_len = key_len;
  ctx->nonce = 0;

  return CRYPTO_OK;
}

crypto_error_t xor_crypt(xor_ctx_t *ctx, const void *input, void *output, size_t len) {
  if (!ctx || !input || !output) return CRYPTO_ERROR_PARAM;
  if (ctx->key_len == 0) return CRYPTO_ERROR_KEY;

  const uint8_t *in = (const uint8_t *)input;
  uint8_t *out = (uint8_t *)output;

  for (size_t i = 0; i < len; i++) {
    size_t key_idx = (i + ctx->nonce) % ctx->key_len;
    out[i] = in[i] ^ ctx->key[key_idx];
  }

  ctx->nonce += (uint32_t)len;
  return CRYPTO_OK;
}

crypto_error_t xor_crypt_simple(const char *key, const void *input, void *output, size_t len) {
  if (!key || !input || !output) return CRYPTO_ERROR_PARAM;

  xor_ctx_t ctx;
  crypto_error_t ret = xor_init(&ctx, key, strlen(key));
  if (ret != CRYPTO_OK) return ret;

  return xor_crypt(&ctx, input, output, len);
}

/* ========================================================================== */
/*                              数据脱敏实现 */
/* ========================================================================== */

crypto_error_t mask_phone(const char *phone, char *output, size_t output_size) {
  if (!phone || !output || output_size < 12) return CRYPTO_ERROR_PARAM;

  size_t len = strlen(phone);
  if (len < 7) {
    strncpy(output, "****", output_size - 1);
    output[output_size - 1] = '\0';
    return CRYPTO_OK;
  }

  snprintf(output, output_size, "%.3s****%s", phone, phone + len - 4);
  return CRYPTO_OK;
}

crypto_error_t mask_email(const char *email, char *output, size_t output_size) {
  if (!email || !output || output_size < 8) return CRYPTO_ERROR_PARAM;

  const char *at = strchr(email, '@');
  if (!at) {
    strncpy(output, "***", output_size - 1);
    output[output_size - 1] = '\0';
    return CRYPTO_OK;
  }

  size_t user_len = (size_t)(at - email);
  if (user_len <= 1) {
    snprintf(output, output_size, "*%s", at);
  } else if (user_len <= 3) {
    snprintf(output, output_size, "%c***%s", email[0], at);
  } else {
    snprintf(output, output_size, "%c%.*s***%c%s",
             email[0], (int)(user_len > 2 ? 1 : 0), email + 1,
             at[-1], at);
  }

  return CRYPTO_OK;
}

crypto_error_t mask_password(const char *password, char *output, size_t output_size) {
  if (!password || !output || output_size < 3) return CRYPTO_ERROR_PARAM;

  size_t len = strlen(password);
  size_t mask_len = len < 8 ? 8 : len;
  if (mask_len >= output_size) mask_len = output_size - 1;

  memset(output, '*', mask_len);
  output[mask_len] = '\0';

  return CRYPTO_OK;
}

crypto_error_t mask_ip(const char *ip, char *output, size_t output_size) {
  if (!ip || !output || output_size < 16) return CRYPTO_ERROR_PARAM;

  const char *last_dot = strrchr(ip, '.');
  if (!last_dot) {
    strncpy(output, "***.***.***.***", output_size - 1);
    output[output_size - 1] = '\0';
    return CRYPTO_OK;
  }

  size_t prefix_len = (size_t)(last_dot - ip + 1);
  if (prefix_len + 3 >= output_size) prefix_len = output_size - 4;

  memcpy(output, ip, prefix_len);
  memset(output + prefix_len, '*', 3);
  output[prefix_len + 3] = '\0';

  return CRYPTO_OK;
}

crypto_error_t data_mask(const char *input, mask_rule_t rule,
                         char *output, size_t output_size) {
  if (!input || !output) return CRYPTO_ERROR_PARAM;

  switch (rule) {
  case MASK_PHONE:
    return mask_phone(input, output, output_size);
  case MASK_EMAIL:
    return mask_email(input, output, output_size);
  case MASK_PASSWORD:
    return mask_password(input, output, output_size);
  case MASK_IP:
    return mask_ip(input, output, output_size);
  default:
    return mask_password(input, output, output_size);
  }
}

crypto_error_t mask_json_field(const char *json_str, const char *field_name,
                               char *output, size_t output_size) {
  if (!json_str || !field_name || !output || output_size < 4) {
    return CRYPTO_ERROR_PARAM;
  }

  char search[128];
  snprintf(search, sizeof(search), "\"%s\":\"", field_name);

  const char *field_start = strstr(json_str, search);
  if (!field_start) {
    strncpy(output, json_str, output_size - 1);
    output[output_size - 1] = '\0';
    return CRYPTO_OK;
  }

  const char *value_start = field_start + strlen(search);
  const char *value_end = strchr(value_start, '"');
  if (!value_end) {
    strncpy(output, json_str, output_size - 1);
    output[output_size - 1] = '\0';
    return CRYPTO_OK;
  }

  size_t prefix_len = (size_t)(value_start - json_str);
  size_t suffix_len = strlen(value_end);

  if (prefix_len + 6 + suffix_len >= output_size) {
    return CRYPTO_ERROR_BUFFER;
  }

  memcpy(output, json_str, prefix_len);
  memset(output + prefix_len, '*', 4);
  memcpy(output + prefix_len + 4, value_end, suffix_len);
  output[prefix_len + 4 + suffix_len] = '\0';

  return CRYPTO_OK;
}

/* ========================================================================== */
/*                              安全内存操作实现 */
/* ========================================================================== */

void secure_memzero(void *ptr, size_t size) {
  if (!ptr || size == 0) return;

  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (size--) {
    *p++ = 0;
  }
}

int secure_memcmp(const void *a, const void *b, size_t len) {
  if (!a || !b) return -1;

  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  uint8_t result = 0;

  for (size_t i = 0; i < len; i++) {
    result |= pa[i] ^ pb[i];
  }

  return (int)result;
}

/* ========================================================================== */
/*                              密钥派生实现 */
/* ========================================================================== */

crypto_error_t derive_key(const char *password, const void *salt, size_t salt_len,
                          int iterations, void *key, size_t key_len) {
  if (!password || !key || key_len == 0 || iterations <= 0) {
    return CRYPTO_ERROR_PARAM;
  }

  uint8_t hash[SHA256_HASH_SIZE];
  uint8_t *out = (uint8_t *)key;
  size_t generated = 0;

  while (generated < key_len) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);

    if (salt && salt_len > 0) {
      sha256_update(&ctx, salt, salt_len);
    }
    sha256_update(&ctx, password, strlen(password));

    uint8_t block[SHA256_HASH_SIZE];
    sha256_final(&ctx, block);

    for (int i = 1; i < iterations; i++) {
      sha256_calc(block, SHA256_HASH_SIZE, hash);
      memcpy(block, hash, SHA256_HASH_SIZE);
    }

    size_t copy_len = key_len - generated;
    if (copy_len > SHA256_HASH_SIZE) copy_len = SHA256_HASH_SIZE;

    memcpy(out + generated, block, copy_len);
    generated += copy_len;

    secure_memzero(block, sizeof(block));
    secure_memzero(hash, sizeof(hash));
  }

  return CRYPTO_OK;
}

crypto_error_t generate_random(void *buf, size_t len) {
  if (!buf || len == 0) return CRYPTO_ERROR_PARAM;

  static int seeded = 0;
  if (!seeded) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    seeded = 1;
  }

  uint8_t *p = (uint8_t *)buf;
  for (size_t i = 0; i < len; i++) {
    p[i] = (uint8_t)(rand() & 0xFF);
  }

  return CRYPTO_OK;
}
