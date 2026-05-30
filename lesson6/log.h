/**
 * @file log.h
 * @brief 日志系统模块
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 提供统一的日志输出功能，支持不同日志级别。
 * 可控制日志输出开关和输出目标（控制台/文件）。
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              日志级别定义 */
/* ========================================================================== */

/** @brief 日志级别枚举 */
typedef enum {
  LOG_LEVEL_DEBUG = 0, /**< 调试信息 */
  LOG_LEVEL_INFO,      /**< 一般信息 */
  LOG_LEVEL_WARN,      /**< 警告信息 */
  LOG_LEVEL_ERROR,     /**< 错误信息 */
  LOG_LEVEL_FATAL,     /**< 致命错误 */
  LOG_LEVEL_NONE       /**< 关闭日志 */
} log_level_t;

/* ========================================================================== */
/*                              日志宏定义 */
/* ========================================================================== */

/** @brief 获取当前日志级别 */
#define LOG_GET_LEVEL() log_get_level()

/** @brief 设置日志级别 */
#define LOG_SET_LEVEL(level) log_set_level(level)

/** @brief 调试日志 - Release模式下通过NDEBUG宏禁用 */
#ifdef NDEBUG
#define LOG_DEBUG(fmt, ...) ((void)0)
#else
#define LOG_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
    if (LOG_GET_LEVEL() <= LOG_LEVEL_DEBUG) {                                  \
      log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, fmt,        \
                ##__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)
#endif

/** @brief 信息日志 */
#define LOG_INFO(fmt, ...)                                                     \
  do {                                                                         \
    if (LOG_GET_LEVEL() <= LOG_LEVEL_INFO) {                                   \
      log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, fmt,         \
                ##__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)

/** @brief 警告日志 */
#define LOG_WARN(fmt, ...)                                                     \
  do {                                                                         \
    if (LOG_GET_LEVEL() <= LOG_LEVEL_WARN) {                                   \
      log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __FUNCTION__, fmt,         \
                ##__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)

/** @brief 错误日志 */
#define LOG_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    if (LOG_GET_LEVEL() <= LOG_LEVEL_ERROR) {                                  \
      log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, fmt,        \
                ##__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)

/** @brief 致命错误日志 */
#define LOG_FATAL(fmt, ...)                                                    \
  do {                                                                         \
    if (LOG_GET_LEVEL() <= LOG_LEVEL_FATAL) {                                  \
      log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, __FUNCTION__, fmt,        \
                ##__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/*                              函数声明 */
/* ========================================================================== */

/**
 * @brief 初始化日志系统
 * @param level 日志级别
 * @param log_file 日志文件路径（NULL表示只输出到控制台）
 * @return 0成功, -1失败
 */
int log_init(log_level_t level, const char *log_file);

/**
 * @brief 关闭日志系统
 */
void log_close(void);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
void log_set_level(log_level_t level);

/**
 * @brief 获取当前日志级别
 * @return 当前日志级别
 */
log_level_t log_get_level(void);

/**
 * @brief 写入日志
 * @param level 日志级别
 * @param file 源文件名
 * @param line 行号
 * @param func 函数名
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
void log_write(log_level_t level, const char *file, int line, const char *func,
               const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
