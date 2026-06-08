/**
 * @file system_monitor.c
 * @brief 系统运行状态监控模块实现
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 实现系统运行状态监控功能，通过读取/proc文件系统获取系统指标。
 * 适用于Linux嵌入式系统。
 */

#include "system_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 模块初始化标志 */
static int sysmon_initialized = 0;

/** @brief 上次CPU采样数据 */
typedef struct {
  unsigned long long user;
  unsigned long long nice;
  unsigned long long system;
  unsigned long long idle;
  unsigned long long iowait;
  unsigned long long irq;
  unsigned long long softirq;
  unsigned long long steal;
} cpu_sample_t;

/** @brief 上次CPU采样 */
static cpu_sample_t last_cpu_sample = {0};

/** @brief 是否为首次采样 */
static int first_cpu_sample = 1;

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 读取CPU采样数据
 * @param sample 输出采样数据
 * @return 0成功, -1失败
 */
static int read_cpu_sample(cpu_sample_t *sample) {
  FILE *fp = fopen("/proc/stat", "r");
  if (!fp) {
    return -1;
  }

  char line[256];
  if (fgets(line, sizeof(line), fp) == NULL) {
    fclose(fp);
    return -1;
  }
  fclose(fp);

  /* 解析 "cpu  user nice system idle iowait irq softirq steal" */
  int ret = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &sample->user, &sample->nice, &sample->system,
                   &sample->idle, &sample->iowait, &sample->irq,
                   &sample->softirq, &sample->steal);
  if (ret < 4) {
    return -1;
  }

  return 0;
}

/**
 * @brief 计算两次CPU采样之间的使用率
 * @param prev 前一次采样
 * @param curr 当前采样
 * @return CPU使用率（百分比）
 */
static double calculate_cpu_usage(const cpu_sample_t *prev,
                                   const cpu_sample_t *curr) {
  unsigned long long prev_idle = prev->idle + prev->iowait;
  unsigned long long curr_idle = curr->idle + curr->iowait;

  unsigned long long prev_total = prev->user + prev->nice + prev->system +
                                   prev->idle + prev->iowait + prev->irq +
                                   prev->softirq + prev->steal;
  unsigned long long curr_total = curr->user + curr->nice + curr->system +
                                   curr->idle + curr->iowait + curr->irq +
                                   curr->softirq + curr->steal;

  unsigned long long total_diff = curr_total - prev_total;
  unsigned long long idle_diff = curr_idle - prev_idle;

  if (total_diff == 0) {
    return 0.0;
  }

  return (double)(total_diff - idle_diff) * 100.0 / (double)total_diff;
}

/* ========================================================================== */
/*                              公共接口实现 */
/* ========================================================================== */

sysmon_error_t sysmon_init(void) {
  first_cpu_sample = 1;
  memset(&last_cpu_sample, 0, sizeof(last_cpu_sample));
  sysmon_initialized = 1;
  return SYSMON_OK;
}

sysmon_error_t sysmon_get_cpu_usage(double *usage_percent) {
  if (usage_percent == NULL) {
    return SYSMON_ERROR;
  }

  cpu_sample_t current;
  if (read_cpu_sample(&current) != 0) {
    return SYSMON_ERROR_READ;
  }

  if (first_cpu_sample) {
    *usage_percent = 0.0;
    first_cpu_sample = 0;
  } else {
    *usage_percent = calculate_cpu_usage(&last_cpu_sample, &current);
  }

  last_cpu_sample = current;
  return SYSMON_OK;
}

sysmon_error_t sysmon_get_memory_info(unsigned long *total_kb,
                                       unsigned long *available_kb,
                                       double *usage_percent) {
  if (total_kb == NULL || available_kb == NULL || usage_percent == NULL) {
    return SYSMON_ERROR;
  }

  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp) {
    return SYSMON_ERROR_OPEN;
  }

  char line[256];
  unsigned long mem_total = 0;
  unsigned long mem_available = 0;
  int found_total = 0;
  int found_available = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) {
      found_total = 1;
    } else if (sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1) {
      found_available = 1;
    }
    if (found_total && found_available) {
      break;
    }
  }

  fclose(fp);

  if (!found_total || !found_available) {
    return SYSMON_ERROR_PARSE;
  }

  *total_kb = mem_total;
  *available_kb = mem_available;
  *usage_percent = (double)(mem_total - mem_available) * 100.0 / (double)mem_total;

  return SYSMON_OK;
}

sysmon_error_t sysmon_get_load_average(double *avg1, double *avg5, double *avg15) {
  if (avg1 == NULL || avg5 == NULL || avg15 == NULL) {
    return SYSMON_ERROR;
  }

  FILE *fp = fopen("/proc/loadavg", "r");
  if (!fp) {
    return SYSMON_ERROR_OPEN;
  }

  if (fscanf(fp, "%lf %lf %lf", avg1, avg5, avg15) != 3) {
    fclose(fp);
    return SYSMON_ERROR_PARSE;
  }

  fclose(fp);
  return SYSMON_OK;
}

sysmon_error_t sysmon_get_uptime(double *seconds) {
  if (seconds == NULL) {
    return SYSMON_ERROR;
  }

  FILE *fp = fopen("/proc/uptime", "r");
  if (!fp) {
    return SYSMON_ERROR_OPEN;
  }

  if (fscanf(fp, "%lf", seconds) != 1) {
    fclose(fp);
    return SYSMON_ERROR_PARSE;
  }

  fclose(fp);
  return SYSMON_OK;
}

sysmon_error_t sysmon_get_status(sysmon_status_t *status) {
  if (status == NULL) {
    return SYSMON_ERROR;
  }

  memset(status, 0, sizeof(sysmon_status_t));

  /* 获取CPU使用率 */
  sysmon_error_t ret = sysmon_get_cpu_usage(&status->cpu_usage_percent);
  if (ret != SYSMON_OK) {
    return ret;
  }

  /* 获取内存信息 */
  ret = sysmon_get_memory_info(&status->mem_total_kb, &status->mem_available_kb,
                                &status->mem_usage_percent);
  if (ret != SYSMON_OK) {
    return ret;
  }

  /* 获取系统负载 */
  ret = sysmon_get_load_average(&status->load_avg_1min, &status->load_avg_5min,
                                 &status->load_avg_15min);
  if (ret != SYSMON_OK) {
    return ret;
  }

  /* 获取运行时间 */
  ret = sysmon_get_uptime(&status->uptime_seconds);
  if (ret != SYSMON_OK) {
    return ret;
  }

  /* 计算天、小时、分钟 */
  unsigned long total_seconds = (unsigned long)status->uptime_seconds;
  status->uptime_days = total_seconds / 86400;
  status->uptime_hours = (total_seconds % 86400) / 3600;
  status->uptime_minutes = (total_seconds % 3600) / 60;

  /* 记录时间戳 */
  status->timestamp = time(NULL);

  return SYSMON_OK;
}

sysmon_error_t sysmon_status_to_json(const sysmon_status_t *status, char *buf,
                                      int buf_size) {
  if (status == NULL || buf == NULL || buf_size <= 0) {
    return SYSMON_ERROR;
  }

  int ret = snprintf(buf, buf_size,
                     "{"
                     "\"cpu_usage\":%.1f,"
                     "\"memory\":{\"total_kb\":%lu,\"available_kb\":%lu,\"usage_percent\":%.1f},"
                     "\"load_avg\":{\"1min\":%.2f,\"5min\":%.2f,\"15min\":%.2f},"
                     "\"uptime\":{\"seconds\":%.0f,\"days\":%lu,\"hours\":%u,\"minutes\":%u},"
                     "\"timestamp\":%ld"
                     "}",
                     status->cpu_usage_percent,
                     status->mem_total_kb, status->mem_available_kb,
                     status->mem_usage_percent,
                     status->load_avg_1min, status->load_avg_5min,
                     status->load_avg_15min,
                     status->uptime_seconds, status->uptime_days,
                     status->uptime_hours, status->uptime_minutes,
                     (long)status->timestamp);

  if (ret < 0 || ret >= buf_size) {
    return SYSMON_ERROR;
  }

  return SYSMON_OK;
}

const char *sysmon_get_error_string(sysmon_error_t error) {
  switch (error) {
  case SYSMON_OK:
    return "Success";
  case SYSMON_ERROR:
    return "Generic error";
  case SYSMON_ERROR_OPEN:
    return "Failed to open proc file";
  case SYSMON_ERROR_READ:
    return "Failed to read proc file";
  case SYSMON_ERROR_PARSE:
    return "Failed to parse proc data";
  default:
    return "Unknown error";
  }
}
