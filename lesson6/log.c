/**
 * @file log.c
 * @brief 日志系统模块实现
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 */

#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 当前日志级别 */
static log_level_t current_level = LOG_LEVEL_INFO;

/** @brief 日志文件指针 */
static FILE *log_fp = NULL;

/** @brief 日志互斥锁 */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief 日志级别名称 */
static const char *level_names[] = {"DEBUG", "INFO",  "WARN",
                                    "ERROR", "FATAL", "NONE"};

/* ========================================================================== */
/*                              函数实现 */
/* ========================================================================== */

/**
 * @brief 初始化日志系统
 * @param level 日志级别
 * @param log_file 日志文件路径（NULL表示只输出到控制台）
 * @return 0成功, -1失败
 */
int log_init(log_level_t level, const char *log_file) {
  current_level = level;

  if (log_file) {
    log_fp = fopen(log_file, "a");
    if (!log_fp) {
      fprintf(stderr, "Failed to open log file: %s\n", log_file);
      return -1;
    }
  }

  return 0;
}

/**
 * @brief 关闭日志系统
 */
void log_close(void) {
  if (log_fp) {
    fclose(log_fp);
    log_fp = NULL;
  }
}

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
void log_set_level(log_level_t level) { current_level = level; }

/**
 * @brief 获取当前日志级别
 * @return 当前日志级别
 */
log_level_t log_get_level(void) { return current_level; }

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
               const char *fmt, ...) {
  va_list args;
  time_t now;
  struct tm *tm_info;
  char time_buf[32];
  char msg_buf[1024];

  /* 获取当前时间 */
  time(&now);
  tm_info = localtime(&now);
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

  /* 格式化消息 */
  va_start(args, fmt);
  vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
  va_end(args);

  pthread_mutex_lock(&log_mutex);

  /* 输出到控制台 */
  fprintf(stdout, "[%s] [%s] [%s:%d %s] %s\n", time_buf, level_names[level],
          file, line, func, msg_buf);
  fflush(stdout);

  /* 输出到文件 */
  if (log_fp) {
    fprintf(log_fp, "[%s] [%s] [%s:%d %s] %s\n", time_buf, level_names[level],
            file, line, func, msg_buf);
    fflush(log_fp);
  }

  pthread_mutex_unlock(&log_mutex);
}
