/**
 * @file device_discovery.h
 * @brief 设备发现模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供局域网设备发现功能，支持：
 * - UDP广播发现
 * - 设备注册和注销
 * - 设备状态查询
 * - 设备分组管理
 */

#ifndef DEVICE_DISCOVERY_H
#define DEVICE_DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 最大设备数量 */
#define DISCOVERY_MAX_DEVICES 32

/** @brief 设备名称最大长度 */
#define DISCOVERY_MAX_NAME_LEN 64

/** @brief 设备IP最大长度 */
#define DISCOVERY_MAX_IP_LEN 16

/** @brief 设备ID最大长度 */
#define DISCOVERY_MAX_ID_LEN 32

/** @brief 发现端口 */
#define DISCOVERY_PORT 5555

/** @brief 广播间隔（秒） */
#define DISCOVERY_BROADCAST_INTERVAL 10

/** @brief 设备超时时间（秒） */
#define DISCOVERY_TIMEOUT 30

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 设备发现错误码 */
typedef enum {
  DISCOVERY_OK = 0,               /**< 操作成功 */
  DISCOVERY_ERROR = -1,           /**< 通用错误 */
  DISCOVERY_ERROR_PARAM = -2,     /**< 参数错误 */
  DISCOVERY_ERROR_NOT_FOUND = -3, /**< 设备未找到 */
  DISCOVERY_ERROR_FULL = -4,      /**< 设备已满 */
  DISCOVERY_ERROR_ALREADY = -5,   /**< 设备已存在 */
  DISCOVERY_ERROR_SOCKET = -6,    /**< Socket错误 */
  DISCOVERY_ERROR_BIND = -7,      /**< 绑定错误 */
  DISCOVERY_ERROR_SEND = -8,      /**< 发送错误 */
} discovery_error_t;

/* ========================================================================== */
/*                              设备状态定义 */
/* ========================================================================== */

/** @brief 设备状态 */
typedef enum {
  DEVICE_STATUS_OFFLINE = 0, /**< 离线 */
  DEVICE_STATUS_ONLINE,      /**< 在线 */
  DEVICE_STATUS_UNKNOWN,     /**< 未知 */
} device_status_t;

/** @brief 设备类型 */
typedef enum {
  DEVICE_TYPE_UNKNOWN = 0,   /**< 未知类型 */
  DEVICE_TYPE_SENSOR,        /**< 传感器 */
  DEVICE_TYPE_ACTUATOR,      /**< 执行器 */
  DEVICE_TYPE_GATEWAY,       /**< 网关 */
  DEVICE_TYPE_CAMERA,        /**< 摄像头 */
  DEVICE_TYPE_DISPLAY,       /**< 显示器 */
} device_type_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief 设备信息 */
typedef struct {
  char id[DISCOVERY_MAX_ID_LEN];       /**< 设备ID（MAC地址） */
  char name[DISCOVERY_MAX_NAME_LEN];   /**< 设备名称 */
  char ip[DISCOVERY_MAX_IP_LEN];       /**< 设备IP地址 */
  uint16_t port;                       /**< 设备端口 */
  device_type_t type;                  /**< 设备类型 */
  device_status_t status;              /**< 设备状态 */
  uint32_t capabilities;               /**< 设备能力（位掩码） */
  long last_seen;                      /**< 最后发现时间 */
  long first_seen;                     /**< 首次发现时间 */
  char firmware_version[16];           /**< 固件版本 */
  char hardware_version[16];           /**< 硬件版本 */
} device_info_t;

/** @brief 设备发现配置 */
typedef struct {
  uint16_t port;                       /**< 监听端口 */
  int broadcast_interval;              /**< 广播间隔（秒） */
  int timeout;                         /**< 设备超时时间（秒） */
  bool enable_broadcast;               /**< 启用广播 */
  bool enable_listen;                  /**< 启用监听 */
  char group[32];                      /**< 设备分组 */
} discovery_config_t;

/** @brief 设备发现回调函数类型 */
typedef void (*discovery_callback_t)(const device_info_t *device, void *user_data);

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化设备发现模块
 * @param config 配置参数（NULL使用默认配置）
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_init(const discovery_config_t *config);

/**
 * @brief 清理设备发现模块资源
 */
void discovery_cleanup(void);

/**
 * @brief 启动设备发现
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_start(void);

/**
 * @brief 停止设备发现
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_stop(void);

/**
 * @brief 手动发送发现广播
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_broadcast(void);

/**
 * @brief 注册本地设备
 * @param info 设备信息
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_register(const device_info_t *info);

/**
 * @brief 注销本地设备
 * @param id 设备ID
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_unregister(const char *id);

/**
 * @brief 获取设备信息
 * @param id 设备ID
 * @param info 输出设备信息
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_get_device(const char *id, device_info_t *info);

/**
 * @brief 获取所有设备列表
 * @param devices 输出设备信息数组
 * @param max_count 数组最大容量
 * @param count 实际设备数量
 * @return DISCOVERY_OK成功
 */
discovery_error_t discovery_get_devices(device_info_t *devices, int max_count,
                                        int *count);

/**
 * @brief 获取在线设备数量
 * @return 在线设备数量
 */
int discovery_get_online_count(void);

/**
 * @brief 清理超时设备
 * @return 清理的设备数量
 */
int discovery_cleanup_timeout(void);

/**
 * @brief 设置发现回调
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void discovery_set_callback(discovery_callback_t callback, void *user_data);

/**
 * @brief 获取设备状态字符串
 * @param status 设备状态
 * @return 状态字符串
 */
const char *discovery_get_status_string(device_status_t status);

/**
 * @brief 获取设备类型字符串
 * @param type 设备类型
 * @return 类型字符串
 */
const char *discovery_get_type_string(device_type_t type);

/**
 * @brief 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *discovery_get_error_string(discovery_error_t error);

/**
 * @brief 打印设备列表
 */
void discovery_print_devices(void);

/**
 * @brief 获取发现状态
 * @return true运行中, false已停止
 */
bool discovery_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_DISCOVERY_H */
