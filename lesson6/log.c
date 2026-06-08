/**
 * @file log.c
 * @brief 日志系统模块实现
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.1
 *
 * 支持日志文件轮转功能，防止单个日志文件过大。
 */

#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 当前日志级别 */
static log_level_t current_level = LOG_LEVEL_INFO;

/** @brief 日志文件指针 */
static FILE *log_fp = NULL;

/** @brief 日志文件路径（用于轮转） */
static char *log_file_path = NULL;

/** @brief 单个日志文件最大大小（字节） */
static unsigned long max_file_size = LOG_DEFAULT_MAX_FILE_SIZE;

/** @brief 最大备份文件数量 */
static int max_backup_count = LOG_DEFAULT_MAX_BACKUP_COUNT;

/** @brief 当前日志文件大小（字节） */
static unsigned long current_file_size = 0;

/** @brief 日志互斥锁 */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief 日志级别名称 */
static const char *level_names[] = {"DEBUG", "INFO",  "WARN",
                                    "ERROR", "FATAL", "NONE"};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 执行日志文件轮转
 * @return 0成功, -1失败
 *
 * 轮转策略：
 * 1. 删除最旧的备份文件（如 log.3）
 * 2. 依次重命名：log.2 -> log.3, log.1 -> log.2, log -> log.1
 * 3. 创建新的日志文件
 */
static int log_rotate(void) {
  if (!log_file_path) {
    return -1;
  }

  /* 关闭当前日志文件 */
  if (log_fp) {
    fclose(log_fp);
    log_fp = NULL;
  }

  /* 删除最旧的备份文件 */
  char old_path[512];
  snprintf(old_path, sizeof(old_path), "%s.%d", log_file_path, max_backup_count);
  unlink(old_path);

  /* 依次重命名备份文件 */
  for (int i = max_backup_count - 1; i >= 1; i--) {
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s.%d", log_file_path, i);
    snprintf(dst, sizeof(dst), "%s.%d", log_file_path, i + 1);
    rename(src, dst);
  }

  /* 将当前日志文件重命名为 .1 */
  char backup_path[512];
  snprintf(backup_path, sizeof(backup_path), "%s.1", log_file_path);
  rename(log_file_path, backup_path);

  /* 创建新的日志文件 */
  log_fp = fopen(log_file_path, "a");
  if (!log_fp) {
    fprintf(stderr, "Failed to create new log file after rotation: %s\n",
            log_file_path);
    return -1;
  }

  current_file_size = 0;
  return 0;
}

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
  return log_init_ex(level, log_file, LOG_DEFAULT_MAX_FILE_SIZE,
                     LOG_DEFAULT_MAX_BACKUP_COUNT);
}

/**
 * @brief 初始化日志系统（带轮转配置）
 * @param level 日志级别
 * @param log_file 日志文件路径（NULL表示只输出到控制台）
 * @param max_size 单个日志文件最大大小（字节）
 * @param backup_count 最大备份文件数量
 * @return 0成功, -1失败
 */
int log_init_ex(log_level_t level, const char *log_file,
                unsigned long max_size, int backup_count) {
  current_level = level;
  max_file_size = max_size;
  max_backup_count = backup_count;

  /* 释放之前的文件路径 */
  if (log_file_path) {
    free(log_file_path);
    log_file_path = NULL;
  }

  if (log_file) {
    /* 保存文件路径（用于轮转） */
    log_file_path = strdup(log_file);
    if (!log_file_path) {
      fprintf(stderr, "Failed to allocate memory for log file path\n");
      return -1;
    }

    /* 打开日志文件 */
    log_fp = fopen(log_file, "a");
    if (!log_fp) {
      fprintf(stderr, "Failed to open log file: %s\n", log_file);
      free(log_file_path);
      log_file_path = NULL;
      return -1;
    }

    /* 获取当前文件大小 */
    struct stat st;
    if (stat(log_file, &st) == 0) {
      current_file_size = st.st_size;
    } else {
      current_file_size = 0;
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
  if (log_file_path) {
    free(log_file_path);
    log_file_path = NULL;
  }
  current_file_size = 0;
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
 * @brief 获取日志文件当前大小
 * @return 文件大小（字节），未打开文件返回0
 */
unsigned long log_get_file_size(void) { return current_file_size; }

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
  int msg_len;

  /* 获取当前时间 */
  time(&now);
  tm_info = localtime(&now);
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

  /* 格式化消息 */
  va_start(args, fmt);
  msg_len = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
  va_end(args);

  if (msg_len < 0) {
    return;
  }

  pthread_mutex_lock(&log_mutex);

  /* 输出到控制台 */
  fprintf(stdout, "[%s] [%s] [%s:%d %s] %s\n", time_buf, level_names[level],
          file, line, func, msg_buf);
  fflush(stdout);

  /* 输出到文件（带轮转检查） */
  if (log_fp) {
    /* 计算本次写入的长度（包含时间戳、级别等） */
    int line_len = snprintf(NULL, 0, "[%s] [%s] [%s:%d %s] %s\n", time_buf,
                            level_names[level], file, line, func, msg_buf);
    if (line_len < 0) {
      line_len = 0;
    }

    /* 检查是否需要轮转 */
    if (current_file_size + (unsigned long)line_len > max_file_size) {
      if (log_rotate() != 0) {
        /* 轮转失败，降级到只输出控制台 */
        pthread_mutex_unlock(&log_mutex);
        return;
      }
    }

    fprintf(log_fp, "[%s] [%s] [%s:%d %s] %s\n", time_buf, level_names[level],
            file, line, func, msg_buf);
    fflush(log_fp);
    current_file_size += (unsigned long)line_len;
  }

  pthread_mutex_unlock(&log_mutex);
}
