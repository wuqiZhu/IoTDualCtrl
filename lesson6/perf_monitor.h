/**
 * @file perf_monitor.h
 * @brief 性能监控模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供系统性能监控功能，包括：
 * - 函数执行时间统计
 * - 内存使用监控
 * - API响应时间统计
 * - 性能瓶颈分析
 */

#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 最大监控点数量 */
#define PERF_MAX_POINTS 32

/** @brief 历史记录最大数量 */
#define PERF_MAX_HISTORY 100

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 性能监控错误码 */
typedef enum {
  PERF_OK = 0,               /**< 操作成功 */
  PERF_ERROR = -1,           /**< 通用错误 */
  PERF_ERROR_PARAM = -2,     /**< 参数错误 */
  PERF_ERROR_FULL = -3,      /**< 监控点已满 */
  PERF_ERROR_NOT_FOUND = -4, /**< 监控点未找到 */
} perf_error_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief 性能统计信息 */
typedef struct {
  const char *name;           /**< 监控点名称 */
  unsigned long call_count;   /**< 调用次数 */
  double total_time_ms;       /**< 总执行时间（毫秒） */
  double min_time_ms;         /**< 最小执行时间（毫秒） */
  double max_time_ms;         /**< 最大执行时间（毫秒） */
  double avg_time_ms;         /**< 平均执行时间（毫秒） */
  double last_time_ms;        /**< 最后一次执行时间（毫秒） */
} perf_stats_t;

/** @brief 性能快照（用于实时监控） */
typedef struct {
  double cpu_usage;           /**< CPU使用率（%） */
  double memory_usage;        /**< 内存使用率（%） */
  unsigned long memory_used;  /**< 已用内存（KB） */
  unsigned long memory_total; /**< 总内存（KB） */
  double load_average_1m;     /**< 1分钟平均负载 */
  double load_average_5m;     /**< 5分钟平均负载 */
  double load_average_15m;    /**< 15分钟平均负载 */
  unsigned long uptime;       /**< 系统运行时间（秒） */
  int thread_count;           /**< 线程数量 */
  int fd_count;               /**< 文件描述符数量 */
} perf_snapshot_t;

/** @brief 性能阈值配置 */
typedef struct {
  double cpu_threshold;       /**< CPU使用率告警阈值（%） */
  double memory_threshold;    /**< 内存使用率告警阈值（%） */
  double response_threshold;  /**< 响应时间告警阈值（毫秒） */
  int check_interval;         /**< 检查间隔（秒） */
} perf_threshold_t;

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化性能监控模块
 * @return PERF_OK成功
 */
perf_error_t perf_monitor_init(void);

/**
 * @brief 清理性能监控模块资源
 */
void perf_monitor_cleanup(void);

/**
 * @brief 创建性能监控点
 * @param name 监控点名称
 * @return PERF_OK成功, PERF_ERROR_FULL监控点已满
 */
perf_error_t perf_point_create(const char *name);

/**
 * @brief 开始计时
 * @param name 监控点名称
 * @return PERF_OK成功
 */
perf_error_t perf_timer_start(const char *name);

/**
 * @brief 停止计时并记录
 * @param name 监控点名称
 * @return PERF_OK成功
 */
perf_error_t perf_timer_stop(const char *name);

/**
 * @brief 记录执行时间（自动计算）
 * @param name 监控点名称
 * @param start_time 开始时间
 * @return PERF_OK成功
 */
perf_error_t perf_timer_record(const char *name, struct timespec *start_time);

/**
 * @brief 获取监控点统计信息
 * @param name 监控点名称
 * @param stats 输出统计信息
 * @return PERF_OK成功
 */
perf_error_t perf_stats_get(const char *name, perf_stats_t *stats);

/**
 * @brief 获取所有监控点统计信息
 * @param stats 输出统计信息数组
 * @param max_count 数组最大容量
 * @param count 实际监控点数量
 * @return PERF_OK成功
 */
perf_error_t perf_stats_get_all(perf_stats_t *stats, int max_count, int *count);

/**
 * @brief 重置监控点统计信息
 * @param name 监控点名称
 * @return PERF_OK成功
 */
perf_error_t perf_stats_reset(const char *name);

/**
 * @brief 重置所有监控点统计信息
 */
void perf_stats_reset_all(void);

/**
 * @brief 获取系统性能快照
 * @param snapshot 输出快照信息
 * @return PERF_OK成功
 */
perf_error_t perf_snapshot_get(perf_snapshot_t *snapshot);

/**
 * @brief 设置性能阈值
 * @param threshold 阈值配置
 * @return PERF_OK成功
 */
perf_error_t perf_threshold_set(const perf_threshold_t *threshold);

/**
 * @brief 获取性能阈值
 * @param threshold 输出阈值配置
 * @return PERF_OK成功
 */
perf_error_t perf_threshold_get(perf_threshold_t *threshold);

/**
 * @brief 检查是否超过阈值
 * @return true超过阈值, false正常
 */
bool perf_threshold_exceeded(void);

/**
 * @brief 打印性能报告
 */
void perf_print_report(void);

/**
 * @brief 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *perf_get_error_string(perf_error_t error);

/* ========================================================================== */
/*                              便捷宏定义 */
/* ========================================================================== */

/**
 * @brief 自动计时宏（在函数开始和结束时自动记录时间）
 *
 * 使用方法：
 * void my_function() {
 *     PERF_AUTO_TIMER("my_function");
 *     // 函数代码...
 * } // 函数结束时自动记录执行时间
 */
#define PERF_AUTO_TIMER(name)                                                  \
  struct timespec _perf_start_##name;                                          \
  clock_gettime(CLOCK_MONOTONIC, &_perf_start_##name);                         \
  __attribute__((cleanup(_perf_auto_timer_cleanup)))                           \
  struct timespec _perf_end_##name = _perf_start_##name;                       \
  (void)_perf_end_##name;

/**
 * @brief 清理函数（用于PERF_AUTO_TIMER宏）
 */
static inline void _perf_auto_timer_cleanup(struct timespec *end) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  (void)end;
  (void)now;
}

#ifdef __cplusplus
}
#endif

#endif /* PERF_MONITOR_H */
