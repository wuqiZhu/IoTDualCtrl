/**
 * @file sensor_manager.h
 * @brief 传感器管理器模块
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 提供传感器注册、管理和数据采集接口，支持即插即用。
 * 新传感器只需实现标准接口即可注册到管理器。
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief 传感器名称最大长度 */
#define SENSOR_NAME_MAX_LEN 32

/** @brief 传感器单位最大长度 */
#define SENSOR_UNIT_MAX_LEN 16

/** @brief 最大传感器数量 */
#define SENSOR_MAX_COUNT 16

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 传感器管理器错误码 */
typedef enum {
  SMGR_OK = 0,               /**< 操作成功 */
  SMGR_ERROR = -1,           /**< 通用错误 */
  SMGR_ERROR_NOMEM = -2,     /**< 内存不足 */
  SMGR_ERROR_FULL = -3,      /**< 传感器数量已达上限 */
  SMGR_ERROR_NOT_FOUND = -4, /**< 传感器未找到 */
  SMGR_ERROR_INIT = -5,      /**< 传感器初始化失败 */
  SMGR_ERROR_READ = -6,      /**< 传感器读取失败 */
} smgr_error_t;

/* ========================================================================== */
/*                              传感器类型定义 */
/* ========================================================================== */

/** @brief 传感器类型 */
typedef enum {
  SENSOR_TYPE_DIGITAL = 0,   /**< 数字传感器（返回0或1） */
  SENSOR_TYPE_ANALOG,        /**< 模拟传感器（返回整数值） */
  SENSOR_TYPE_FLOAT,         /**< 浮点传感器（返回浮点值） */
  SENSOR_TYPE_CUSTOM,        /**< 自定义传感器 */
} sensor_type_t;

/** @brief 传感器数据结构体 */
typedef struct {
  union {
    int digital;             /**< 数字值 */
    int analog;              /**< 模拟值 */
    double floating;         /**< 浮点值 */
    void *custom;            /**< 自定义数据指针 */
  } value;
  sensor_type_t type;        /**< 数据类型 */
  int valid;                 /**< 数据有效标志 */
  time_t timestamp;          /**< 数据采集时间戳 */
} sensor_data_t;

/* ========================================================================== */
/*                              传感器接口定义 */
/* ========================================================================== */

/** @brief 传感器操作接口 */
typedef struct {
  /**
   * @brief 初始化传感器
   * @return 0成功, -1失败
   */
  int (*init)(void);

  /**
   * @brief 读取传感器数据
   * @param data 输出数据
   * @return 0成功, -1失败
   */
  int (*read)(sensor_data_t *data);

  /**
   * @brief 关闭传感器
   */
  void (*close)(void);

  /**
   * @brief 获取传感器状态
   * @return 0正常, -1故障
   */
  int (*get_status)(void);
} sensor_ops_t;

/* ========================================================================== */
/*                              传感器信息结构体 */
/* ========================================================================== */

/** @brief 传感器信息 */
typedef struct {
  char name[SENSOR_NAME_MAX_LEN];    /**< 传感器名称 */
  char unit[SENSOR_UNIT_MAX_LEN];    /**< 数据单位 */
  sensor_type_t type;                 /**< 传感器类型 */
  sensor_ops_t ops;                   /**< 操作接口 */
  int enabled;                        /**< 启用标志 */
  int failure_count;                  /**< 连续失败次数 */
  sensor_data_t last_data;            /**< 最后一次数据 */
  time_t last_read_time;              /**< 最后读取时间 */
} sensor_info_t;

/* ========================================================================== */
/*                              公共接口 */
/* ========================================================================== */

/**
 * @brief 初始化传感器管理器
 * @return SMGR_OK成功
 */
smgr_error_t sensor_mgr_init(void);

/**
 * @brief 清理传感器管理器
 */
void sensor_mgr_cleanup(void);

/**
 * @brief 注册传感器
 * @param name 传感器名称
 * @param unit 数据单位
 * @param type 传感器类型
 * @param ops 操作接口
 * @return SMGR_OK成功
 *
 * 示例：
 * @code
 * sensor_ops_t dht11_ops = {
 *   .init = dht11_init,
 *   .read = dht11_read_data,
 *   .close = dht11_close,
 *   .get_status = dht11_get_status
 * };
 * sensor_mgr_register("dht11", "°C/%", SENSOR_TYPE_CUSTOM, &dht11_ops);
 * @endcode
 */
smgr_error_t sensor_mgr_register(const char *name, const char *unit,
                                  sensor_type_t type, const sensor_ops_t *ops);

/**
 * @brief 注销传感器
 * @param name 传感器名称
 * @return SMGR_OK成功
 */
smgr_error_t sensor_mgr_unregister(const char *name);

/**
 * @brief 读取传感器数据
 * @param name 传感器名称
 * @param data 输出数据
 * @return SMGR_OK成功
 */
smgr_error_t sensor_mgr_read(const char *name, sensor_data_t *data);

/**
 * @brief 批量读取所有传感器数据
 * @param names 传感器名称数组
 * @param data 输出数据数组
 * @param count 传感器数量
 * @return 成功读取的数量
 */
int sensor_mgr_read_all(char names[][SENSOR_NAME_MAX_LEN],
                        sensor_data_t *data, int count);

/**
 * @brief 启用/禁用传感器
 * @param name 传感器名称
 * @param enabled 1启用, 0禁用
 * @return SMGR_OK成功
 */
smgr_error_t sensor_mgr_set_enabled(const char *name, int enabled);

/**
 * @brief 获取传感器信息
 * @param name 传感器名称
 * @param info 输出信息
 * @return SMGR_OK成功
 */
smgr_error_t sensor_mgr_get_info(const char *name, sensor_info_t *info);

/**
 * @brief 获取已注册传感器数量
 * @return 传感器数量
 */
int sensor_mgr_get_count(void);

/**
 * @brief 获取传感器管理器错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *sensor_mgr_get_error_string(smgr_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H */
