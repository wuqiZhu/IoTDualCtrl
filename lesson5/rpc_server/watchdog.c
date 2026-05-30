/**
 * @file watchdog.c
 * @brief 软件看门狗模块实现
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 使用独立线程实现软件看门狗，监控主程序运行状态。
 * 主程序定期喂狗，超过阈值未喂狗则触发超时处理。
 */

#include "watchdog.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief 看门狗模块状态 */
typedef struct {
  bool initialized;            /**< 初始化标志 */
  bool running;                /**< 运行标志 */
  unsigned int timeout_sec;    /**< 超时时间（秒） */
  time_t last_feed_time;       /**< 上次喂狗时间 */
  pthread_t monitor_thread;    /**< 监控线程 */
  pthread_mutex_t lock;        /**< 互斥锁 */
  pthread_cond_t cond;         /**< 条件变量 */
  watchdog_timeout_callback_t callback; /**< 超时回调函数 */
  void *user_data;             /**< 用户数据 */
} watchdog_context_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 看门狗全局上下文 */
static watchdog_context_t g_watchdog = {
    .initialized = false,
    .running = false,
    .timeout_sec = WATCHDOG_DEFAULT_TIMEOUT_SEC,
    .last_feed_time = 0,
    .monitor_thread = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .callback = NULL,
    .user_data = NULL,
};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 默认超时处理函数
 * @param user_data 未使用
 *
 * 打印错误信息并调用 exit(1) 终止程序。
 */
static void default_timeout_handler(void *user_data) {
  (void)user_data;
  fprintf(stderr, "[WATCHDOG] FATAL: Program watchdog timeout! "
                  "Program appears to be hung. Exiting...\n");
  fflush(stderr);
  exit(1);
}

/**
 * @brief 看门狗监控线程函数
 * @param arg 未使用
 * @return NULL
 *
 * 定期检查看门狗是否超时，超时则触发回调函数。
 */
static void *watchdog_monitor_thread(void *arg) {
  (void)arg;

  printf("[WATCHDOG] Monitor thread started, timeout=%u sec\n",
         g_watchdog.timeout_sec);

  while (1) {
    pthread_mutex_lock(&g_watchdog.lock);

    /* 等待一段时间或收到停止信号 */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; /* 每秒检查一次 */

    int ret = pthread_cond_timedwait(&g_watchdog.cond, &g_watchdog.lock, &ts);

    /* 检查是否收到停止信号 */
    if (!g_watchdog.running) {
      pthread_mutex_unlock(&g_watchdog.lock);
      printf("[WATCHDOG] Monitor thread received stop signal\n");
      break;
    }

    /* 超时检查（忽略 ETIMEDOUT，这是正常的超时返回） */
    if (ret != 0 && ret != ETIMEDOUT) {
      pthread_mutex_unlock(&g_watchdog.lock);
      fprintf(stderr, "[WATCHDOG] pthread_cond_timedwait failed: %d\n", ret);
      break;
    }

    /* 检查看门狗是否超时 */
    time_t now = time(NULL);
    time_t elapsed = now - g_watchdog.last_feed_time;

    if (elapsed >= (time_t)g_watchdog.timeout_sec) {
      printf("[WATCHDOG] Timeout detected! Elapsed: %ld sec, Threshold: %u sec\n",
             elapsed, g_watchdog.timeout_sec);
      pthread_mutex_unlock(&g_watchdog.lock);

      /* 调用超时回调函数 */
      if (g_watchdog.callback) {
        g_watchdog.callback(g_watchdog.user_data);
      } else {
        default_timeout_handler(g_watchdog.user_data);
      }
      break;
    }

    pthread_mutex_unlock(&g_watchdog.lock);
  }

  printf("[WATCHDOG] Monitor thread exiting\n");
  return NULL;
}

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

watchdog_error_t watchdog_init(unsigned int timeout_sec,
                               watchdog_timeout_callback_t callback,
                               void *user_data) {
  pthread_mutex_lock(&g_watchdog.lock);

  if (g_watchdog.initialized) {
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_ALREADY_STARTED;
  }

  /* 参数验证 */
  if (timeout_sec == 0) {
    timeout_sec = WATCHDOG_DEFAULT_TIMEOUT_SEC;
  }

  if (timeout_sec < WATCHDOG_MIN_TIMEOUT_SEC ||
      timeout_sec > WATCHDOG_MAX_TIMEOUT_SEC) {
    fprintf(stderr, "[WATCHDOG] Invalid timeout: %u sec (range: %u-%u)\n",
            timeout_sec, WATCHDOG_MIN_TIMEOUT_SEC, WATCHDOG_MAX_TIMEOUT_SEC);
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_INVALID_PARAM;
  }

  /* 初始化上下文 */
  g_watchdog.timeout_sec = timeout_sec;
  g_watchdog.callback = callback;
  g_watchdog.user_data = user_data;
  g_watchdog.last_feed_time = time(NULL);
  g_watchdog.running = false;
  g_watchdog.initialized = true;

  pthread_mutex_unlock(&g_watchdog.lock);

  printf("[WATCHDOG] Initialized with timeout=%u sec\n", timeout_sec);
  return WATCHDOG_OK;
}

watchdog_error_t watchdog_start(void) {
  pthread_mutex_lock(&g_watchdog.lock);

  if (!g_watchdog.initialized) {
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_NOT_STARTED;
  }

  if (g_watchdog.running) {
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_ALREADY_STARTED;
  }

  /* 重置喂狗时间 */
  g_watchdog.last_feed_time = time(NULL);
  g_watchdog.running = true;

  /* 创建监控线程 */
  int ret = pthread_create(&g_watchdog.monitor_thread, NULL,
                           watchdog_monitor_thread, NULL);
  if (ret != 0) {
    fprintf(stderr, "[WATCHDOG] Failed to create monitor thread: %d\n", ret);
    g_watchdog.running = false;
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_THREAD_CREATE;
  }

  /* 分离线程，线程结束后自动释放资源 */
  pthread_detach(g_watchdog.monitor_thread);

  pthread_mutex_unlock(&g_watchdog.lock);

  printf("[WATCHDOG] Started\n");
  return WATCHDOG_OK;
}

watchdog_error_t watchdog_stop(void) {
  pthread_mutex_lock(&g_watchdog.lock);

  if (!g_watchdog.running) {
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_NOT_STARTED;
  }

  /* 发送停止信号 */
  g_watchdog.running = false;
  pthread_cond_signal(&g_watchdog.cond);

  pthread_mutex_unlock(&g_watchdog.lock);

  /* 等待线程退出（由于已分离，这里只是给线程时间退出） */
  usleep(100000); /* 100ms */

  printf("[WATCHDOG] Stopped\n");
  return WATCHDOG_OK;
}

watchdog_error_t watchdog_feed(void) {
  pthread_mutex_lock(&g_watchdog.lock);

  if (!g_watchdog.initialized) {
    pthread_mutex_unlock(&g_watchdog.lock);
    return WATCHDOG_ERROR_NOT_STARTED;
  }

  g_watchdog.last_feed_time = time(NULL);

  pthread_mutex_unlock(&g_watchdog.lock);
  return WATCHDOG_OK;
}

bool watchdog_is_running(void) {
  return g_watchdog.running;
}

const char *watchdog_get_error_string(watchdog_error_t error) {
  switch (error) {
  case WATCHDOG_OK:
    return "Success";
  case WATCHDOG_ERROR:
    return "Generic error";
  case WATCHDOG_ERROR_INVALID_PARAM:
    return "Invalid parameter";
  case WATCHDOG_ERROR_ALREADY_STARTED:
    return "Watchdog already started";
  case WATCHDOG_ERROR_NOT_STARTED:
    return "Watchdog not started";
  case WATCHDOG_ERROR_THREAD_CREATE:
    return "Failed to create monitor thread";
  case WATCHDOG_ERROR_TIMEOUT:
    return "Watchdog timeout";
  default:
    return "Unknown error";
  }
}

void watchdog_cleanup(void) {
  /* 停止看门狗 */
  watchdog_stop();

  /* 销毁同步原语 */
  pthread_mutex_destroy(&g_watchdog.lock);
  pthread_cond_destroy(&g_watchdog.cond);

  /* 重置状态 */
  g_watchdog.initialized = false;

  printf("[WATCHDOG] Cleanup completed\n");
}
