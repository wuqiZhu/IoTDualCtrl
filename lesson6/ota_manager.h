/**
 * @file ota_manager.h
 * @brief OTA远程升级管理模块
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 提供OTA远程升级功能，包括：
 * - 固件下载（HTTP/HTTPS）
 * - 固件校验（MD5/SHA256）
 * - 固件备份和回滚
 * - 升级状态管理
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief 固件版本最大长度 */
#define OTA_VERSION_MAX_LEN 32

/** @brief 固件URL最大长度 */
#define OTA_URL_MAX_LEN 256

/** @brief 固件校验和最大长度 */
#define OTA_CHECKSUM_MAX_LEN 64

/** @brief 固件下载路径 */
#define OTA_DOWNLOAD_PATH "/tmp/firmware.bin"

/** @brief 固件备份路径 */
#define OTA_BACKUP_PATH "/etc/device/firmware_backup.bin"

/** @brief 当前固件路径 */
#define OTA_CURRENT_PATH "/usr/local/bin/mqtt_bridge"

/** @brief OTA状态文件 */
#define OTA_STATE_FILE "/etc/device/ota_state.json"

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief OTA错误码 */
typedef enum {
  OTA_OK = 0,                /**< 操作成功 */
  OTA_ERROR = -1,            /**< 通用错误 */
  OTA_ERROR_DOWNLOAD = -2,   /**< 下载失败 */
  OTA_ERROR_CHECKSUM = -3,   /**< 校验失败 */
  OTA_ERROR_BACKUP = -4,     /**< 备份失败 */
  OTA_ERROR_INSTALL = -5,    /**< 安装失败 */
  OTA_ERROR_ROLLBACK = -6,   /**< 回滚失败 */
  OTA_ERROR_INVALID = -7,    /**< 无效参数 */
  OTA_ERROR_BUSY = -8,       /**< 升级进行中 */
} ota_error_t;

/* ========================================================================== */
/*                              OTA状态定义 */
/* ========================================================================== */

/** @brief OTA状态 */
typedef enum {
  OTA_STATE_IDLE = 0,        /**< 空闲 */
  OTA_STATE_DOWNLOADING,     /**< 下载中 */
  OTA_STATE_VERIFYING,       /**< 校验中 */
  OTA_STATE_INSTALLING,      /**< 安装中 */
  OTA_STATE_REBOOTING,       /**< 重启中 */
  OTA_STATE_ROLLBACK,        /**< 回滚中 */
  OTA_STATE_SUCCESS,         /**< 升级成功 */
  OTA_STATE_FAILED,          /**< 升级失败 */
} ota_state_t;

/* ========================================================================== */
/*                              OTA信息结构体 */
/* ========================================================================== */

/** @brief OTA升级信息 */
typedef struct {
  char version[OTA_VERSION_MAX_LEN];       /**< 目标版本 */
  char url[OTA_URL_MAX_LEN];               /**< 固件下载URL */
  char checksum[OTA_CHECKSUM_MAX_LEN];     /**< 固件校验和（MD5或SHA256） */
  int checksum_type;                        /**< 校验和类型：0=MD5, 1=SHA256 */
  int force;                                /**< 强制升级标志 */
} ota_info_t;

/** @brief OTA状态信息 */
typedef struct {
  ota_state_t state;                        /**< 当前状态 */
  int progress;                             /**< 进度百分比（0-100） */
  char current_version[OTA_VERSION_MAX_LEN]; /**< 当前版本 */
  char target_version[OTA_VERSION_MAX_LEN];  /**< 目标版本 */
  char error_msg[128];                      /**< 错误信息 */
  int retry_count;                          /**< 重试次数 */
} ota_status_t;

/* ========================================================================== */
/*                              公共接口 */
/* ========================================================================== */

/**
 * @brief 初始化OTA管理器
 * @return OTA_OK成功
 */
ota_error_t ota_init(void);

/**
 * @brief 清理OTA管理器
 */
void ota_cleanup(void);

/**
 * @brief 开始OTA升级
 * @param info 升级信息
 * @return OTA_OK成功
 *
 * 异步执行升级流程：
 * 1. 下载固件
 * 2. 校验固件
 * 3. 备份当前固件
 * 4. 安装新固件
 * 5. 重启服务
 *
 * 如果升级失败，自动回滚到备份固件。
 */
ota_error_t ota_start_upgrade(const ota_info_t *info);

/**
 * @brief 获取OTA状态
 * @param status 输出状态信息
 * @return OTA_OK成功
 */
ota_error_t ota_get_status(ota_status_t *status);

/**
 * @brief 手动回滚到备份固件
 * @return OTA_OK成功
 */
ota_error_t ota_rollback(void);

/**
 * @brief 检查是否有可用更新
 * @param current_version 当前版本
 * @param latest_version 输出最新版本
 * @param version_size 缓冲区大小
 * @return 1有更新, 0无更新, -1检查失败
 *
 * 通过MQTT查询云端是否有新版本可用。
 */
int ota_check_update(const char *current_version, char *latest_version,
                     int version_size);

/**
 * @brief 获取当前固件版本
 * @param version 输出版本缓冲区
 * @param size 缓冲区大小
 * @return OTA_OK成功
 */
ota_error_t ota_get_current_version(char *version, int size);

/**
 * @brief 获取OTA错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *ota_get_error_string(ota_error_t error);

/**
 * @brief 获取OTA状态描述
 * @param state 状态码
 * @return 状态描述字符串
 */
const char *ota_get_state_string(ota_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
