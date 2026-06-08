/**
 * @file security_audit.c
 * @brief 安全审计模块实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现安全审计功能，包括安全事件记录、登录监控、证书检查等。
 */

#define _GNU_SOURCE
#include "security_audit.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief IP锁定信息 */
typedef struct {
  char ip[16];                 /**< IP地址 */
  int attempt_count;           /**< 尝试次数 */
  time_t lock_time;            /**< 锁定时间 */
  bool is_locked;              /**< 是否锁定 */
} ip_lock_t;

/** @brief 安全审计上下文 */
typedef struct {
  audit_entry_t entries[AUDIT_MAX_ENTRIES]; /**< 审计条目数组 */
  int entry_count;                          /**< 条目数量 */
  int entry_index;                          /**< 当前索引（环形缓冲） */
  audit_stats_t stats;                      /**< 统计信息 */
  ip_lock_t locks[32];                      /**< IP锁定表 */
  int lock_count;                           /**< 锁定数量 */
  pthread_mutex_t lock;                     /**< 互斥锁 */
  bool initialized;                         /**< 初始化标志 */
} audit_context_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 安全审计全局上下文 */
static audit_context_t g_audit = {
    .entry_count = 0,
    .entry_index = 0,
    .stats = {0},
    .lock_count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 查找IP锁定记录
 * @param ip IP地址
 * @return 锁定索引, -1未找到
 */
static int find_ip_lock(const char *ip) {
  if (!ip) return -1;

  for (int i = 0; i < g_audit.lock_count; i++) {
    if (strcmp(g_audit.locks[i].ip, ip) == 0) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief 写入审计日志到文件
 * @param entry 审计条目
 */
static void write_to_log_file(const audit_entry_t *entry) {
  FILE *fp = fopen(AUDIT_LOG_FILE, "a");
  if (!fp) return;

  char time_str[64];
  struct tm *tm_info = localtime(&entry->timestamp);
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(fp, "[%s] [%s] [%s] source=%s user=%s: %s\n",
          time_str,
          audit_get_level_string(entry->level),
          audit_get_event_string(entry->event_type),
          entry->source,
          entry->user,
          entry->description);

  if (strlen(entry->details) > 0) {
    fprintf(fp, "  Details: %s\n", entry->details);
  }

  fclose(fp);
}

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

audit_error_t security_audit_init(void) {
  pthread_mutex_lock(&g_audit.lock);

  if (g_audit.initialized) {
    pthread_mutex_unlock(&g_audit.lock);
    return AUDIT_OK;
  }

  memset(&g_audit.entries, 0, sizeof(g_audit.entries));
  memset(&g_audit.stats, 0, sizeof(g_audit.stats));
  memset(&g_audit.locks, 0, sizeof(g_audit.locks));
  g_audit.entry_count = 0;
  g_audit.entry_index = 0;
  g_audit.lock_count = 0;
  g_audit.initialized = true;

  pthread_mutex_unlock(&g_audit.lock);

  /* 记录系统启动事件 */
  audit_log_event(AUDIT_EVENT_SYSTEM_START, AUDIT_LEVEL_INFO,
                  "system", "root", "Security audit module initialized", "");

  printf("[AUDIT] Security audit module initialized\n");
  return AUDIT_OK;
}

void security_audit_cleanup(void) {
  pthread_mutex_lock(&g_audit.lock);

  /* 记录系统停止事件 */
  audit_log_event(AUDIT_EVENT_SYSTEM_STOP, AUDIT_LEVEL_INFO,
                  "system", "root", "Security audit module stopping", "");

  g_audit.initialized = false;

  pthread_mutex_unlock(&g_audit.lock);

  printf("[AUDIT] Security audit module cleanup completed\n");
}

audit_error_t audit_log_event(audit_event_type_t event_type, audit_level_t level,
                              const char *source, const char *user,
                              const char *description, const char *details) {
  if (!source || !user || !description) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);

  if (!g_audit.initialized) {
    pthread_mutex_unlock(&g_audit.lock);
    return AUDIT_ERROR;
  }

  /* 创建审计条目 */
  audit_entry_t *entry = &g_audit.entries[g_audit.entry_index];
  entry->timestamp = time(NULL);
  entry->event_type = event_type;
  entry->level = level;
  strncpy(entry->source, source, sizeof(entry->source) - 1);
  entry->source[sizeof(entry->source) - 1] = '\0';
  strncpy(entry->user, user, sizeof(entry->user) - 1);
  entry->user[sizeof(entry->user) - 1] = '\0';
  strncpy(entry->description, description, sizeof(entry->description) - 1);
  entry->description[sizeof(entry->description) - 1] = '\0';

  if (details) {
    strncpy(entry->details, details, sizeof(entry->details) - 1);
    entry->details[sizeof(entry->details) - 1] = '\0';
  } else {
    entry->details[0] = '\0';
  }

  /* 更新索引和计数 */
  g_audit.entry_index = (g_audit.entry_index + 1) % AUDIT_MAX_ENTRIES;
  if (g_audit.entry_count < AUDIT_MAX_ENTRIES) {
    g_audit.entry_count++;
  }

  /* 更新统计信息 */
  g_audit.stats.total_events++;

  /* 写入日志文件 */
  write_to_log_file(entry);

  pthread_mutex_unlock(&g_audit.lock);

  /* 控制台输出 */
  printf("[AUDIT][%s] %s: %s\n",
         audit_get_level_string(level),
         audit_get_event_string(event_type),
         description);

  return AUDIT_OK;
}

audit_error_t audit_log_login(const char *source, const char *user, bool success) {
  if (!source || !user) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);

  /* 更新统计信息 */
  g_audit.stats.login_attempts++;

  if (success) {
    g_audit.stats.last_login = time(NULL);
    pthread_mutex_unlock(&g_audit.lock);

    return audit_log_event(AUDIT_EVENT_LOGIN_SUCCESS, AUDIT_LEVEL_INFO,
                           source, user, "Login successful", "");
  } else {
    g_audit.stats.login_failures++;
    g_audit.stats.last_failure = time(NULL);

    /* 检查是否需要锁定 */
    int lock_idx = find_ip_lock(source);
    if (lock_idx < 0 && g_audit.lock_count < 32) {
      lock_idx = g_audit.lock_count++;
      strncpy(g_audit.locks[lock_idx].ip, source, 15);
      g_audit.locks[lock_idx].ip[15] = '\0';
      g_audit.locks[lock_idx].attempt_count = 0;
      g_audit.locks[lock_idx].is_locked = false;
    }

    if (lock_idx >= 0) {
      g_audit.locks[lock_idx].attempt_count++;

      if (g_audit.locks[lock_idx].attempt_count >= MAX_LOGIN_ATTEMPTS) {
        g_audit.locks[lock_idx].is_locked = true;
        g_audit.locks[lock_idx].lock_time = time(NULL);

        pthread_mutex_unlock(&g_audit.lock);

        char details[128];
        snprintf(details, sizeof(details), "IP locked after %d failed attempts",
                 MAX_LOGIN_ATTEMPTS);

        return audit_log_event(AUDIT_EVENT_LOGIN_LOCKOUT, AUDIT_LEVEL_WARNING,
                               source, user, "Account locked due to too many failures",
                               details);
      }
    }

    pthread_mutex_unlock(&g_audit.lock);

    return audit_log_event(AUDIT_EVENT_LOGIN_FAILED, AUDIT_LEVEL_WARNING,
                           source, user, "Login failed", "");
  }
}

audit_error_t audit_log_config_change(const char *source, const char *user,
                                      const char *key, const char *old_value,
                                      const char *new_value) {
  if (!source || !user || !key || !old_value || !new_value) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);
  g_audit.stats.config_changes++;
  pthread_mutex_unlock(&g_audit.lock);

  char details[512];
  snprintf(details, sizeof(details), "key=%s, old=%s, new=%s",
           key, old_value, new_value);

  return audit_log_event(AUDIT_EVENT_CONFIG_CHANGE, AUDIT_LEVEL_INFO,
                         source, user, "Configuration changed", details);
}

audit_error_t audit_get_stats(audit_stats_t *stats) {
  if (!stats) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);
  memcpy(stats, &g_audit.stats, sizeof(audit_stats_t));
  pthread_mutex_unlock(&g_audit.lock);

  return AUDIT_OK;
}

audit_error_t audit_get_recent(audit_entry_t *entries, int max_count, int *count) {
  if (!entries || max_count <= 0 || !count) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);

  *count = 0;
  int idx = (g_audit.entry_index - 1 + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;

  for (int i = 0; i < g_audit.entry_count && *count < max_count; i++) {
    if (g_audit.entries[idx].timestamp > 0) {
      memcpy(&entries[*count], &g_audit.entries[idx], sizeof(audit_entry_t));
      (*count)++;
    }
    idx = (idx - 1 + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;
  }

  pthread_mutex_unlock(&g_audit.lock);
  return AUDIT_OK;
}

bool audit_is_locked(const char *ip) {
  if (!ip) return false;

  pthread_mutex_lock(&g_audit.lock);

  int idx = find_ip_lock(ip);
  if (idx < 0) {
    pthread_mutex_unlock(&g_audit.lock);
    return false;
  }

  ip_lock_t *lock = &g_audit.locks[idx];
  if (!lock->is_locked) {
    pthread_mutex_unlock(&g_audit.lock);
    return false;
  }

  /* 检查锁定是否过期 */
  time_t now = time(NULL);
  if (difftime(now, lock->lock_time) >= LOGIN_LOCKOUT_TIME) {
    lock->is_locked = false;
    lock->attempt_count = 0;
    pthread_mutex_unlock(&g_audit.lock);
    return false;
  }

  pthread_mutex_unlock(&g_audit.lock);
  return true;
}

audit_error_t audit_clear_lock(const char *ip) {
  if (!ip) {
    return AUDIT_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_audit.lock);

  int idx = find_ip_lock(ip);
  if (idx >= 0) {
    g_audit.locks[idx].is_locked = false;
    g_audit.locks[idx].attempt_count = 0;
  }

  pthread_mutex_unlock(&g_audit.lock);
  return AUDIT_OK;
}

audit_error_t audit_check_certificate(const char *cert_path, cert_info_t *info) {
  if (!cert_path || !info) {
    return AUDIT_ERROR_PARAM;
  }

  memset(info, 0, sizeof(cert_info_t));
  strncpy(info->cert_path, cert_path, sizeof(info->cert_path) - 1);

  /* 使用openssl命令检查证书 */
  char cmd[512];
  char output[1024];

  /* 检查证书是否存在 */
  struct stat st;
  if (stat(cert_path, &st) != 0) {
    info->is_valid = false;
    return AUDIT_ERROR_IO;
  }

  /* 获取证书过期时间 */
  snprintf(cmd, sizeof(cmd), "openssl x509 -in %s -noout -enddate 2>/dev/null",
           cert_path);

  FILE *fp = popen(cmd, "r");
  if (fp) {
    if (fgets(output, sizeof(output), fp)) {
      /* 解析 notAfter=May 31 10:00:00 2026 GMT */
      char *date_str = strstr(output, "notAfter=");
      if (date_str) {
        date_str += 9;
        struct tm tm_info = {0};
        if (strptime(date_str, "%b %d %H:%M:%S %Y %Z", &tm_info)) {
          info->not_after = mktime(&tm_info);
          time_t now = time(NULL);
          info->days_remaining = (int)difftime(info->not_after, now) / 86400;
          info->is_valid = (info->days_remaining > 0);

          /* 检查是否即将过期（30天内） */
          if (info->days_remaining > 0 && info->days_remaining <= 30) {
            audit_log_event(AUDIT_EVENT_CERT_EXPIRING, AUDIT_LEVEL_WARNING,
                            "certificate", "system",
                            "Certificate expiring soon", cert_path);
          } else if (info->days_remaining <= 0) {
            audit_log_event(AUDIT_EVENT_CERT_EXPIRED, AUDIT_LEVEL_ERROR,
                            "certificate", "system",
                            "Certificate has expired", cert_path);
          }
        }
      }
    }
    pclose(fp);
  }

  return AUDIT_OK;
}

int audit_check_all_certificates(void) {
  DIR *dir = opendir(CERT_DIR);
  if (!dir) return 0;

  int expiring_count = 0;
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;

    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".pem") == 0) {
      char cert_path[512];
      snprintf(cert_path, sizeof(cert_path), "%s/%s", CERT_DIR, entry->d_name);

      cert_info_t info;
      if (audit_check_certificate(cert_path, &info) == AUDIT_OK) {
        if (info.days_remaining <= 30) {
          expiring_count++;
        }
      }
    }
  }

  closedir(dir);
  return expiring_count;
}

const char *audit_get_event_string(audit_event_type_t event_type) {
  switch (event_type) {
  case AUDIT_EVENT_LOGIN_SUCCESS: return "LOGIN_SUCCESS";
  case AUDIT_EVENT_LOGIN_FAILED: return "LOGIN_FAILED";
  case AUDIT_EVENT_LOGIN_LOCKOUT: return "LOGIN_LOCKOUT";
  case AUDIT_EVENT_CONFIG_CHANGE: return "CONFIG_CHANGE";
  case AUDIT_EVENT_CERT_EXPIRING: return "CERT_EXPIRING";
  case AUDIT_EVENT_CERT_EXPIRED: return "CERT_EXPIRED";
  case AUDIT_EVENT_UNAUTHORIZED: return "UNAUTHORIZED";
  case AUDIT_EVENT_SYSTEM_START: return "SYSTEM_START";
  case AUDIT_EVENT_SYSTEM_STOP: return "SYSTEM_STOP";
  case AUDIT_EVENT_FIRMWARE_UPDATE: return "FIRMWARE_UPDATE";
  case AUDIT_EVENT_DATA_EXPORT: return "DATA_EXPORT";
  case AUDIT_EVENT_OTHER: return "OTHER";
  default: return "UNKNOWN";
  }
}

const char *audit_get_level_string(audit_level_t level) {
  switch (level) {
  case AUDIT_LEVEL_INFO: return "INFO";
  case AUDIT_LEVEL_WARNING: return "WARN";
  case AUDIT_LEVEL_ERROR: return "ERROR";
  case AUDIT_LEVEL_CRITICAL: return "CRIT";
  default: return "UNKNOWN";
  }
}

void audit_print_report(void) {
  pthread_mutex_lock(&g_audit.lock);

  printf("\n=== Security Audit Report ===\n");
  printf("Total Events: %d\n", g_audit.stats.total_events);
  printf("Login Attempts: %d\n", g_audit.stats.login_attempts);
  printf("Login Failures: %d\n", g_audit.stats.login_failures);
  printf("Config Changes: %d\n", g_audit.stats.config_changes);
  printf("Unauthorized Access: %d\n", g_audit.stats.unauthorized_access);
  printf("IP Locks: %d\n", g_audit.lock_count);
  printf("\nRecent Events:\n");

  int count = 0;
  int idx = (g_audit.entry_index - 1 + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;

  for (int i = 0; i < g_audit.entry_count && count < 10; i++) {
    if (g_audit.entries[idx].timestamp > 0) {
      audit_entry_t *entry = &g_audit.entries[idx];
      char time_str[64];
      struct tm *tm_info = localtime(&entry->timestamp);
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

      printf("  [%s] [%s] %s: %s\n",
             time_str,
             audit_get_level_string(entry->level),
             audit_get_event_string(entry->event_type),
             entry->description);
      count++;
    }
    idx = (idx - 1 + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;
  }

  printf("=============================\n\n");

  pthread_mutex_unlock(&g_audit.lock);
}

audit_error_t audit_export_log(const char *file_path) {
  if (!file_path) {
    return AUDIT_ERROR_PARAM;
  }

  FILE *fp = fopen(file_path, "w");
  if (!fp) {
    return AUDIT_ERROR_IO;
  }

  pthread_mutex_lock(&g_audit.lock);

  fprintf(fp, "Security Audit Log Export\n");
  fprintf(fp, "========================\n\n");

  int idx = (g_audit.entry_index - g_audit.entry_count + AUDIT_MAX_ENTRIES) % AUDIT_MAX_ENTRIES;

  for (int i = 0; i < g_audit.entry_count; i++) {
    audit_entry_t *entry = &g_audit.entries[idx];
    if (entry->timestamp > 0) {
      char time_str[64];
      struct tm *tm_info = localtime(&entry->timestamp);
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

      fprintf(fp, "[%s] [%s] [%s] source=%s user=%s: %s\n",
              time_str,
              audit_get_level_string(entry->level),
              audit_get_event_string(entry->event_type),
              entry->source,
              entry->user,
              entry->description);

      if (strlen(entry->details) > 0) {
        fprintf(fp, "  Details: %s\n", entry->details);
      }
    }
    idx = (idx + 1) % AUDIT_MAX_ENTRIES;
  }

  pthread_mutex_unlock(&g_audit.lock);

  fclose(fp);
  printf("[AUDIT] Log exported to %s\n", file_path);
  return AUDIT_OK;
}

audit_error_t audit_clear_log(void) {
  pthread_mutex_lock(&g_audit.lock);

  memset(&g_audit.entries, 0, sizeof(g_audit.entries));
  memset(&g_audit.stats, 0, sizeof(g_audit.stats));
  g_audit.entry_count = 0;
  g_audit.entry_index = 0;

  pthread_mutex_unlock(&g_audit.lock);

  printf("[AUDIT] Log cleared\n");
  return AUDIT_OK;
}
