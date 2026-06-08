/**
 * @file device_auth.h
 * @brief 设备认证模块
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 提供设备身份认证功能，包括：
 * - 设备ID生成（基于MAC地址）
 * - 设备Token管理
 * - TLS证书路径管理
 * - 设备注册状态管理
 */

#ifndef DEVICE_AUTH_H
#define DEVICE_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief 设备ID最大长度 */
#define DEVICE_ID_MAX_LEN 64

/** @brief 设备Token最大长度 */
#define DEVICE_TOKEN_MAX_LEN 128

/** @brief 证书路径最大长度 */
#define CERT_PATH_MAX_LEN 256

/** @brief 设备凭证文件路径 */
#define DEVICE_CREDENTIAL_FILE "/etc/device/credentials.json"

/** @brief TLS证书目录 */
#define TLS_CERT_DIR "/etc/device/certs"

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 设备认证错误码 */
typedef enum {
  DEVAUTH_OK = 0,               /**< 操作成功 */
  DEVAUTH_ERROR = -1,           /**< 通用错误 */
  DEVAUTH_ERROR_MAC = -2,       /**< 获取MAC地址失败 */
  DEVAUTH_ERROR_FILE = -3,      /**< 文件操作失败 */
  DEVAUTH_ERROR_JSON = -4,      /**< JSON解析失败 */
  DEVAUTH_ERROR_NOT_FOUND = -5, /**< 凭证未找到 */
  DEVAUTH_ERROR_INVALID = -6,   /**< 凭证无效 */
} devauth_error_t;

/* ========================================================================== */
/*                              设备认证状态 */
/* ========================================================================== */

/** @brief 设备认证状态 */
typedef enum {
  DEVAUTH_STATE_UNKNOWN = 0,    /**< 未知状态 */
  DEVAUTH_STATE_REGISTERED,     /**< 已注册（有凭证） */
  DEVAUTH_STATE_UNREGISTERED,   /**< 未注册（无凭证） */
  DEVAUTH_STATE_EXPIRED,        /**< 凭证已过期 */
} devauth_state_t;

/* ========================================================================== */
/*                              设备信息结构体 */
/* ========================================================================== */

/** @brief 设备认证信息 */
typedef struct {
  char device_id[DEVICE_ID_MAX_LEN];           /**< 设备ID（基于MAC地址） */
  char device_token[DEVICE_TOKEN_MAX_LEN];     /**< 设备Token */
  char ca_cert_path[CERT_PATH_MAX_LEN];        /**< CA证书路径 */
  char client_cert_path[CERT_PATH_MAX_LEN];    /**< 客户端证书路径 */
  char client_key_path[CERT_PATH_MAX_LEN];     /**< 客户端私钥路径 */
  devauth_state_t state;                        /**< 认证状态 */
  int use_tls;                                  /**< 是否使用TLS */
} devauth_info_t;

/* ========================================================================== */
/*                              公共接口 */
/* ========================================================================== */

/**
 * @brief 初始化设备认证模块
 * @return DEVAUTH_OK成功
 */
devauth_error_t devauth_init(void);

/**
 * @brief 获取设备ID（基于MAC地址）
 * @param device_id 输出设备ID缓冲区
 * @param size 缓冲区大小
 * @return DEVAUTH_OK成功
 *
 * 设备ID格式：网卡MAC地址（如 "00:1A:2B:3C:4D:5E"）
 * 如果获取MAC失败，使用随机生成的ID
 */
devauth_error_t devauth_get_device_id(char *device_id, int size);

/**
 * @brief 加载设备凭证
 * @param info 输出设备信息
 * @return DEVAUTH_OK成功, DEVAUTH_ERROR_NOT_FOUND凭证文件不存在
 *
 * 从凭证文件加载设备ID、Token、证书路径等信息。
 * 如果凭证文件不存在，返回DEVAUTH_ERROR_NOT_FOUND。
 */
devauth_error_t devauth_load_credentials(devauth_info_t *info);

/**
 * @brief 保存设备凭证
 * @param info 设备信息
 * @return DEVAUTH_OK成功
 *
 * 将设备ID、Token、证书路径等信息保存到凭证文件。
 */
devauth_error_t devauth_save_credentials(const devauth_info_t *info);

/**
 * @brief 注册设备（首次使用）
 * @param info 输出设备信息
 * @return DEVAUTH_OK成功
 *
 * 首次使用时：
 * 1. 生成设备ID（MAC地址）
 * 2. 生成随机Token
 * 3. 保存到凭证文件
 */
devauth_error_t devauth_register_device(devauth_info_t *info);

/**
 * @brief 检查设备是否已注册
 * @return 1已注册, 0未注册
 */
int devauth_is_registered(void);

/**
 * @brief 获取TLS启用状态
 * @return 1启用TLS, 0禁用TLS
 *
 * 从环境变量MQTT_USE_TLS读取，默认禁用。
 */
int devauth_is_tls_enabled(void);

/**
 * @brief 获取设备认证错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *devauth_get_error_string(devauth_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_AUTH_H */
