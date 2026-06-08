/**
 * @file system_monitor.h
 * @brief 系统运行状态监控模块
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 提供系统运行状态监控功能，包括CPU使用率、内存使用率、系统负载、运行时间等。
 * 用于心跳上报和健康检查接口。
 */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 系统监控错误码 */
typedef enum {
  SYSMON_OK = 0,           /**< 操作成功 */
  SYSMON_ERROR = -1,       /**< 通用错误 */
  SYSMON_ERROR_OPEN = -2,  /**< 打开文件失败 */
  SYSMON_ERROR_READ = -3,  /**< 读取失败 */
  SYSMON_ERROR_PARSE = -4, /**< 解析失败 */
} sysmon_error_t;

/* ========================================================================== */
/*                              系统状态结构体 */
/* ========================================================================== */

/** @brief 系统运行状态信息 */
typedef struct {
  /* CPU相关 */
  double cpu_usage_percent;     /**< CPU使用率（百分比） */

  /* 内存相关 */
  unsigned long mem_total_kb;   /**< 总内存（KB） */
  unsigned long mem_available_kb; /**< 可用内存（KB） */
  double mem_usage_percent;     /**< 内存使用率（百分比） */

  /* 系统负载 */
  double load_avg_1min;         /**< 1分钟平均负载 */
  double load_avg_5min;         /**< 5分钟平均负载 */
  double load_avg_15min;        /**< 15分钟平均负载 */

  /* 运行时间 */
  double uptime_seconds;        /**< 系统运行时间（秒） */
  unsigned long uptime_days;    /**< 运行天数 */
  unsigned int uptime_hours;    /**< 运行小时 */
  unsigned int uptime_minutes;  /**< 运行分钟 */

  /* 时间戳 */
  time_t timestamp;             /**< 数据采集时间戳 */
} sysmon_status_t;

/* ========================================================================== */
/*                              公共接口 */
/* ========================================================================== */

/**
 * @brief 初始化系统监控模块
 * @return SYSMON_OK成功
 */
sysmon_error_t sysmon_init(void);

/**
 * @brief 获取系统运行状态
 * @param status 输出系统状态结构体
 * @return SYSMON_OK成功
 *
 * 每次调用会重新采集系统指标，建议采集间隔不小于1秒。
 */
sysmon_error_t sysmon_get_status(sysmon_status_t *status);

/**
 * @brief 获取CPU使用率
 * @param usage_percent 输出CPU使用率（百分比）
 * @return SYSMON_OK成功
 *
 * 需要两次采样计算，首次调用返回0。
 */
sysmon_error_t sysmon_get_cpu_usage(double *usage_percent);

/**
 * @brief 获取内存使用情况
 * @param total_kb 输出总内存（KB）
 * @param available_kb 输出可用内存（KB）
 * @param usage_percent 输出使用率（百分比）
 * @return SYSMON_OK成功
 */
sysmon_error_t sysmon_get_memory_info(unsigned long *total_kb,
                                       unsigned long *available_kb,
                                       double *usage_percent);

/**
 * @brief 获取系统负载
 * @param avg1 输出1分钟平均负载
 * @param avg5 输出5分钟平均负载
 * @param avg15 输出15分钟平均负载
 * @return SYSMON_OK成功
 */
sysmon_error_t sysmon_get_load_average(double *avg1, double *avg5, double *avg15);

/**
 * @brief 获取系统运行时间
 * @param seconds 输出运行时间（秒）
 * @return SYSMON_OK成功
 */
sysmon_error_t sysmon_get_uptime(double *seconds);

/**
 * @brief 格式化系统状态为JSON字符串
 * @param status 系统状态
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return SYSMON_OK成功
 *
 * 生成的JSON格式：
 * {
 *   "cpu_usage": 25.5,
 *   "memory": {"total_kb": 512000, "available_kb": 256000, "usage_percent": 50.0},
 *   "load_avg": {"1min": 0.5, "5min": 0.3, "15min": 0.2},
 *   "uptime": {"seconds": 3600, "days": 0, "hours": 1, "minutes": 0},
 *   "timestamp": 1717036800
 * }
 */
sysmon_error_t sysmon_status_to_json(const sysmon_status_t *status, char *buf,
                                      int buf_size);

/**
 * @brief 获取系统监控错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *sysmon_get_error_string(sysmon_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MONITOR_H */
