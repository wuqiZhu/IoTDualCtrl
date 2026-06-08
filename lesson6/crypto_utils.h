/**
 * @file crypto_utils.h
 * @brief 数据安全工具模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供数据安全功能，包括：
 * - 轻量级数据加密/解密（XOR流加密）
 * - SHA-256哈希计算
 * - 敏感数据脱敏
 * - 安全内存擦除
 * - 密钥派生
 */

#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief SHA-256哈希输出长度（字节） */
#define SHA256_HASH_SIZE 32

/** @brief SHA-256哈希十六进制字符串长度 */
#define SHA256_HEX_SIZE 65

/** @brief 最大密钥长度 */
#define CRYPTO_MAX_KEY_SIZE 64

/** @brief 最大脱敏缓冲区大小 */
#define CRYPTO_MASK_BUF_SIZE 256

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 加密工具错误码 */
typedef enum {
  CRYPTO_OK = 0,              /**< 操作成功 */
  CRYPTO_ERROR = -1,          /**< 通用错误 */
  CRYPTO_ERROR_PARAM = -2,    /**< 参数错误 */
  CRYPTO_ERROR_BUFFER = -3,   /**< 缓冲区不足 */
  CRYPTO_ERROR_KEY = -4,      /**< 密钥错误 */
} crypto_error_t;

/* ========================================================================== */
/*                              加密算法类型 */
/* ========================================================================== */

/** @brief 加密算法类型 */
typedef enum {
  CRYPTO_ALGO_XOR = 0,        /**< XOR流加密 */
  CRYPTO_ALGO_AES128 = 1,     /**< AES-128（预留） */
} crypto_algo_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief SHA-256上下文 */
typedef struct {
  uint32_t state[8];          /**< 中间哈希状态 */
  uint64_t count;             /**< 已处理的位数 */
  uint8_t buffer[64];         /**< 数据缓冲区 */
} sha256_ctx_t;

/** @brief XOR加密上下文 */
typedef struct {
  uint8_t key[CRYPTO_MAX_KEY_SIZE]; /**< 密钥 */
  size_t key_len;                    /**< 密钥长度 */
  uint32_t nonce;                    /**< 随机数/计数器 */
} xor_ctx_t;

/** @brief 脱敏规则 */
typedef enum {
  MASK_PHONE = 0,             /**< 手机号脱敏：138****1234 */
  MASK_EMAIL = 1,             /**< 邮箱脱敏：zh***@example.com */
  MASK_PASSWORD = 2,          /**< 密码脱敏：****** */
  MASK_IP = 3,                /**< IP脱敏：192.168.***.*** */
  MASK_CUSTOM = 4,            /**< 自定义脱敏 */
} mask_rule_t;

/* ========================================================================== */
/*                              SHA-256接口 */
/* ========================================================================== */

/**
 * @brief 初始化SHA-256上下文
 * @param ctx SHA-256上下文
 * @return CRYPTO_OK成功
 */
crypto_error_t sha256_init(sha256_ctx_t *ctx);

/**
 * @brief 向SHA-256添加数据
 * @param ctx SHA-256上下文
 * @param data 输入数据
 * @param len 数据长度
 * @return CRYPTO_OK成功
 */
crypto_error_t sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);

/**
 * @brief 完成SHA-256计算，输出哈希值
 * @param ctx SHA-256上下文
 * @param hash 输出哈希（32字节）
 * @return CRYPTO_OK成功
 */
crypto_error_t sha256_final(sha256_ctx_t *ctx, uint8_t hash[SHA256_HASH_SIZE]);

/**
 * @brief 一步计算SHA-256哈希
 * @param data 输入数据
 * @param len 数据长度
 * @param hash 输出哈希（32字节）
 * @return CRYPTO_OK成功
 */
crypto_error_t sha256_calc(const void *data, size_t len, uint8_t hash[SHA256_HASH_SIZE]);

/**
 * @brief 计算SHA-256并输出十六进制字符串
 * @param data 输入数据
 * @param len 数据长度
 * @param hex_out 输出十六进制字符串（至少65字节）
 * @return CRYPTO_OK成功
 */
crypto_error_t sha256_hex(const void *data, size_t len, char hex_out[SHA256_HEX_SIZE]);

/* ========================================================================== */
/*                              XOR加密接口 */
/* ========================================================================== */

/**
 * @brief 初始化XOR加密上下文
 * @param ctx XOR上下文
 * @param key 密钥
 * @param key_len 密钥长度
 * @return CRYPTO_OK成功
 */
crypto_error_t xor_init(xor_ctx_t *ctx, const void *key, size_t key_len);

/**
 * @brief XOR加密/解密数据（对称操作）
 * @param ctx XOR上下文
 * @param input 输入数据
 * @param output 输出数据（可与input相同）
 * @param len 数据长度
 * @return CRYPTO_OK成功
 */
crypto_error_t xor_crypt(xor_ctx_t *ctx, const void *input, void *output, size_t len);

/**
 * @brief 使用默认密钥加密/解密
 * @param key 密钥字符串
 * @param input 输入数据
 * @param output 输出数据
 * @param len 数据长度
 * @return CRYPTO_OK成功
 */
crypto_error_t xor_crypt_simple(const char *key, const void *input, void *output, size_t len);

/* ========================================================================== */
/*                              数据脱敏接口 */
/* ========================================================================== */

/**
 * @brief 脱敏敏感数据
 * @param input 输入字符串
 * @param rule 脱敏规则
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t data_mask(const char *input, mask_rule_t rule,
                         char *output, size_t output_size);

/**
 * @brief 手机号脱敏（保留前3后4）
 * @param phone 手机号
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t mask_phone(const char *phone, char *output, size_t output_size);

/**
 * @brief 邮箱脱敏（用户名保留首尾字符）
 * @param email 邮箱地址
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t mask_email(const char *email, char *output, size_t output_size);

/**
 * @brief 密码脱敏（替换为星号）
 * @param password 密码
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t mask_password(const char *password, char *output, size_t output_size);

/**
 * @brief IP地址脱敏（最后一段替换为星号）
 * @param ip IP地址
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t mask_ip(const char *ip, char *output, size_t output_size);

/**
 * @brief JSON字段脱敏（替换指定字段值）
 * @param json_str JSON字符串
 * @param field_name 字段名
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return CRYPTO_OK成功
 */
crypto_error_t mask_json_field(const char *json_str, const char *field_name,
                               char *output, size_t output_size);

/* ========================================================================== */
/*                              安全内存操作 */
/* ========================================================================== */

/**
 * @brief 安全擦除内存（防止编译器优化）
 * @param ptr 内存指针
 * @param size 内存大小
 */
void secure_memzero(void *ptr, size_t size);

/**
 * @brief 安全内存比较（恒定时间，防时序攻击）
 * @param a 内存块A
 * @param b 内存块B
 * @param len 长度
 * @return 0相等, 非0不相等
 */
int secure_memcmp(const void *a, const void *b, size_t len);

/* ========================================================================== */
/*                              密钥派生接口 */
/* ========================================================================== */

/**
 * @brief 从密码派生密钥（PBKDF2简化版）
 * @param password 密码
 * @param salt 盐值
 * @param salt_len 盐值长度
 * @param iterations 迭代次数
 * @param key 输出密钥
 * @param key_len 密钥长度
 * @return CRYPTO_OK成功
 */
crypto_error_t derive_key(const char *password, const void *salt, size_t salt_len,
                          int iterations, void *key, size_t key_len);

/**
 * @brief 生成随机字节
 * @param buf 输出缓冲区
 * @param len 长度
 * @return CRYPTO_OK成功
 */
crypto_error_t generate_random(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRYPTO_UTILS_H */
