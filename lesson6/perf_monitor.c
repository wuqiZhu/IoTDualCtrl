/**
 * @file perf_monitor.c
 * @brief 性能监控模块实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现系统性能监控功能，包括函数执行时间统计、内存使用监控等。
 */

#include "perf_monitor.h"
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief 监控点 */
typedef struct {
  char name[64];               /**< 监控点名称 */
  bool active;                 /**< 是否激活 */
  struct timespec start_time;  /**< 开始时间 */
  unsigned long call_count;    /**< 调用次数 */
  double total_time_ms;        /**< 总执行时间（毫秒） */
  double min_time_ms;          /**< 最小执行时间（毫秒） */
  double max_time_ms;          /**< 最大执行时间（毫秒） */
  double last_time_ms;         /**< 最后一次执行时间（毫秒） */
} perf_point_t;

/** @brief 性能监控上下文 */
typedef struct {
  perf_point_t points[PERF_MAX_POINTS]; /**< 监控点数组 */
  int point_count;                       /**< 监控点数量 */
  perf_threshold_t threshold;            /**< 阈值配置 */
  pthread_mutex_t lock;                  /**< 互斥锁 */
  bool initialized;                      /**< 初始化标志 */
} perf_context_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 性能监控全局上下文 */
static perf_context_t g_perf = {
    .point_count = 0,
    .threshold =
        {
            .cpu_threshold = 80.0,
            .memory_threshold = 80.0,
            .response_threshold = 100.0,
            .check_interval = 60,
        },
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 查找监控点
 * @param name 监控点名称
 * @return 监控点索引, -1未找到
 */
static int find_point(const char *name) {
  if (!name) {
    return -1;
  }

  for (int i = 0; i < g_perf.point_count; i++) {
    if (g_perf.points[i].active && strcmp(g_perf.points[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief 计算时间差（毫秒）
 * @param start 开始时间
 * @param end 结束时间
 * @return 时间差（毫秒）
 */
static double time_diff_ms(const struct timespec *start,
                           const struct timespec *end) {
  double diff_sec = (double)(end->tv_sec - start->tv_sec);
  double diff_nsec = (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
  return diff_sec * 1000.0 + diff_nsec;
}

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

perf_error_t perf_monitor_init(void) {
  pthread_mutex_lock(&g_perf.lock);

  if (g_perf.initialized) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_OK;
  }

  memset(g_perf.points, 0, sizeof(g_perf.points));
  g_perf.point_count = 0;
  g_perf.initialized = true;

  pthread_mutex_unlock(&g_perf.lock);

  printf("[PERF] Performance monitor initialized\n");
  return PERF_OK;
}

void perf_monitor_cleanup(void) {
  pthread_mutex_lock(&g_perf.lock);

  memset(g_perf.points, 0, sizeof(g_perf.points));
  g_perf.point_count = 0;
  g_perf.initialized = false;

  pthread_mutex_unlock(&g_perf.lock);

  printf("[PERF] Performance monitor cleanup completed\n");
}

perf_error_t perf_point_create(const char *name) {
  if (!name) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);

  if (!g_perf.initialized) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_ERROR;
  }

  /* 检查是否已存在 */
  if (find_point(name) >= 0) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_OK;
  }

  /* 检查是否已满 */
  if (g_perf.point_count >= PERF_MAX_POINTS) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_ERROR_FULL;
  }

  /* 创建新监控点 */
  perf_point_t *point = &g_perf.points[g_perf.point_count];
  strncpy(point->name, name, sizeof(point->name) - 1);
  point->name[sizeof(point->name) - 1] = '\0';
  point->active = true;
  point->call_count = 0;
  point->total_time_ms = 0.0;
  point->min_time_ms = 1e9;
  point->max_time_ms = 0.0;
  point->last_time_ms = 0.0;

  g_perf.point_count++;

  pthread_mutex_unlock(&g_perf.lock);

  printf("[PERF] Created monitoring point: %s\n", name);
  return PERF_OK;
}

perf_error_t perf_timer_start(const char *name) {
  if (!name) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);

  int index = find_point(name);
  if (index < 0) {
    /* 自动创建监控点 */
    if (g_perf.point_count >= PERF_MAX_POINTS) {
      pthread_mutex_unlock(&g_perf.lock);
      return PERF_ERROR_FULL;
    }

    index = g_perf.point_count;
    perf_point_t *point = &g_perf.points[index];
    strncpy(point->name, name, sizeof(point->name) - 1);
    point->name[sizeof(point->name) - 1] = '\0';
    point->active = true;
    point->call_count = 0;
    point->total_time_ms = 0.0;
    point->min_time_ms = 1e9;
    point->max_time_ms = 0.0;
    point->last_time_ms = 0.0;
    g_perf.point_count++;
  }

  clock_gettime(CLOCK_MONOTONIC, &g_perf.points[index].start_time);

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

perf_error_t perf_timer_stop(const char *name) {
  if (!name) {
    return PERF_ERROR_PARAM;
  }

  struct timespec end_time;
  clock_gettime(CLOCK_MONOTONIC, &end_time);

  pthread_mutex_lock(&g_perf.lock);

  int index = find_point(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_ERROR_NOT_FOUND;
  }

  perf_point_t *point = &g_perf.points[index];
  double elapsed = time_diff_ms(&point->start_time, &end_time);

  /* 更新统计信息 */
  point->call_count++;
  point->total_time_ms += elapsed;
  point->last_time_ms = elapsed;

  if (elapsed < point->min_time_ms) {
    point->min_time_ms = elapsed;
  }
  if (elapsed > point->max_time_ms) {
    point->max_time_ms = elapsed;
  }

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

perf_error_t perf_timer_record(const char *name, struct timespec *start_time) {
  if (!name || !start_time) {
    return PERF_ERROR_PARAM;
  }

  struct timespec end_time;
  clock_gettime(CLOCK_MONOTONIC, &end_time);

  pthread_mutex_lock(&g_perf.lock);

  /* 查找或创建监控点 */
  int index = find_point(name);
  if (index < 0) {
    if (g_perf.point_count >= PERF_MAX_POINTS) {
      pthread_mutex_unlock(&g_perf.lock);
      return PERF_ERROR_FULL;
    }

    index = g_perf.point_count;
    perf_point_t *point = &g_perf.points[index];
    strncpy(point->name, name, sizeof(point->name) - 1);
    point->name[sizeof(point->name) - 1] = '\0';
    point->active = true;
    point->call_count = 0;
    point->total_time_ms = 0.0;
    point->min_time_ms = 1e9;
    point->max_time_ms = 0.0;
    point->last_time_ms = 0.0;
    g_perf.point_count++;
  }

  perf_point_t *point = &g_perf.points[index];
  double elapsed = time_diff_ms(start_time, &end_time);

  /* 更新统计信息 */
  point->call_count++;
  point->total_time_ms += elapsed;
  point->last_time_ms = elapsed;

  if (elapsed < point->min_time_ms) {
    point->min_time_ms = elapsed;
  }
  if (elapsed > point->max_time_ms) {
    point->max_time_ms = elapsed;
  }

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

perf_error_t perf_stats_get(const char *name, perf_stats_t *stats) {
  if (!name || !stats) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);

  int index = find_point(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_ERROR_NOT_FOUND;
  }

  perf_point_t *point = &g_perf.points[index];
  stats->name = point->name;
  stats->call_count = point->call_count;
  stats->total_time_ms = point->total_time_ms;
  stats->min_time_ms = (point->call_count > 0) ? point->min_time_ms : 0.0;
  stats->max_time_ms = point->max_time_ms;
  stats->last_time_ms = point->last_time_ms;
  stats->avg_time_ms =
      (point->call_count > 0) ? point->total_time_ms / point->call_count : 0.0;

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

perf_error_t perf_stats_get_all(perf_stats_t *stats, int max_count,
                                int *count) {
  if (!stats || max_count <= 0 || !count) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);

  *count = 0;
  for (int i = 0; i < g_perf.point_count && *count < max_count; i++) {
    if (g_perf.points[i].active) {
      perf_point_t *point = &g_perf.points[i];
      stats[*count].name = point->name;
      stats[*count].call_count = point->call_count;
      stats[*count].total_time_ms = point->total_time_ms;
      stats[*count].min_time_ms =
          (point->call_count > 0) ? point->min_time_ms : 0.0;
      stats[*count].max_time_ms = point->max_time_ms;
      stats[*count].last_time_ms = point->last_time_ms;
      stats[*count].avg_time_ms = (point->call_count > 0)
                                      ? point->total_time_ms / point->call_count
                                      : 0.0;
      (*count)++;
    }
  }

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

perf_error_t perf_stats_reset(const char *name) {
  if (!name) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);

  int index = find_point(name);
  if (index < 0) {
    pthread_mutex_unlock(&g_perf.lock);
    return PERF_ERROR_NOT_FOUND;
  }

  perf_point_t *point = &g_perf.points[index];
  point->call_count = 0;
  point->total_time_ms = 0.0;
  point->min_time_ms = 1e9;
  point->max_time_ms = 0.0;
  point->last_time_ms = 0.0;

  pthread_mutex_unlock(&g_perf.lock);
  return PERF_OK;
}

void perf_stats_reset_all(void) {
  pthread_mutex_lock(&g_perf.lock);

  for (int i = 0; i < g_perf.point_count; i++) {
    g_perf.points[i].call_count = 0;
    g_perf.points[i].total_time_ms = 0.0;
    g_perf.points[i].min_time_ms = 1e9;
    g_perf.points[i].max_time_ms = 0.0;
    g_perf.points[i].last_time_ms = 0.0;
  }

  pthread_mutex_unlock(&g_perf.lock);
}

perf_error_t perf_snapshot_get(perf_snapshot_t *snapshot) {
  if (!snapshot) {
    return PERF_ERROR_PARAM;
  }

  memset(snapshot, 0, sizeof(perf_snapshot_t));

  /* 读取内存信息 */
  FILE *meminfo = fopen("/proc/meminfo", "r");
  if (meminfo) {
    char line[256];
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0;

    while (fgets(line, sizeof(line), meminfo)) {
      if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
        continue;
      }
      if (sscanf(line, "MemFree: %lu kB", &mem_free) == 1) {
        continue;
      }
      if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
        continue;
      }
    }
    fclose(meminfo);

    snapshot->memory_total = mem_total;
    snapshot->memory_used = mem_total - mem_available;
    snapshot->memory_usage =
        (mem_total > 0) ? (double)snapshot->memory_used / mem_total * 100.0
                        : 0.0;
  }

  /* 读取负载信息 */
  FILE *loadavg = fopen("/proc/loadavg", "r");
  if (loadavg) {
    fscanf(loadavg, "%lf %lf %lf", &snapshot->load_average_1m,
           &snapshot->load_average_5m, &snapshot->load_average_15m);
    fclose(loadavg);
  }

  /* 读取运行时间 */
  FILE *uptime = fopen("/proc/uptime", "r");
  if (uptime) {
    fscanf(uptime, "%lu", &snapshot->uptime);
    fclose(uptime);
  }

  /* 读取CPU使用率（简化实现） */
  snapshot->cpu_usage = 0.0; /* 需要更复杂的实现来计算CPU使用率 */

  return PERF_OK;
}

perf_error_t perf_threshold_set(const perf_threshold_t *threshold) {
  if (!threshold) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);
  memcpy(&g_perf.threshold, threshold, sizeof(perf_threshold_t));
  pthread_mutex_unlock(&g_perf.lock);

  printf("[PERF] Threshold updated: CPU=%.1f%%, Memory=%.1f%%, Response=%.1fms\n",
         threshold->cpu_threshold, threshold->memory_threshold,
         threshold->response_threshold);
  return PERF_OK;
}

perf_error_t perf_threshold_get(perf_threshold_t *threshold) {
  if (!threshold) {
    return PERF_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_perf.lock);
  memcpy(threshold, &g_perf.threshold, sizeof(perf_threshold_t));
  pthread_mutex_unlock(&g_perf.lock);

  return PERF_OK;
}

bool perf_threshold_exceeded(void) {
  perf_snapshot_t snapshot;
  if (perf_snapshot_get(&snapshot) != PERF_OK) {
    return false;
  }

  pthread_mutex_lock(&g_perf.lock);
  bool exceeded =
      (snapshot.cpu_usage > g_perf.threshold.cpu_threshold) ||
      (snapshot.memory_usage > g_perf.threshold.memory_threshold);
  pthread_mutex_unlock(&g_perf.lock);

  return exceeded;
}

void perf_print_report(void) {
  pthread_mutex_lock(&g_perf.lock);

  printf("\n=== Performance Report ===\n");
  printf("Monitoring Points: %d\n", g_perf.point_count);
  printf("\n%-20s %10s %12s %12s %12s %12s\n", "Name", "Calls", "Total(ms)",
         "Avg(ms)", "Min(ms)", "Max(ms)");
  printf("--------------------------------------------------------------"
         "----------------\n");

  for (int i = 0; i < g_perf.point_count; i++) {
    if (g_perf.points[i].active) {
      perf_point_t *p = &g_perf.points[i];
      double avg =
          (p->call_count > 0) ? p->total_time_ms / p->call_count : 0.0;
      printf("%-20s %10lu %12.2f %12.2f %12.2f %12.2f\n", p->name,
             p->call_count, p->total_time_ms, avg,
             (p->call_count > 0) ? p->min_time_ms : 0.0, p->max_time_ms);
    }
  }

  /* 打印系统快照 */
  perf_snapshot_t snapshot;
  pthread_mutex_unlock(&g_perf.lock);

  if (perf_snapshot_get(&snapshot) == PERF_OK) {
    printf("\n=== System Snapshot ===\n");
    printf("Memory Usage: %.1f%% (%lu KB / %lu KB)\n", snapshot.memory_usage,
           snapshot.memory_used, snapshot.memory_total);
    printf("Load Average: %.2f %.2f %.2f\n", snapshot.load_average_1m,
           snapshot.load_average_5m, snapshot.load_average_15m);
    printf("Uptime: %lu seconds\n", snapshot.uptime);
  }

  printf("========================\n\n");
}

const char *perf_get_error_string(perf_error_t error) {
  switch (error) {
  case PERF_OK:
    return "Success";
  case PERF_ERROR:
    return "Generic error";
  case PERF_ERROR_PARAM:
    return "Invalid parameter";
  case PERF_ERROR_FULL:
    return "Monitoring points full";
  case PERF_ERROR_NOT_FOUND:
    return "Monitoring point not found";
  default:
    return "Unknown error";
  }
}
