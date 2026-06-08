/**
 * @file data_cache.c
 * @brief 数据缓存模块实现
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 使用环形缓冲区实现数据缓存，支持文件持久化。
 * 用于断网重传场景，保证遥测数据不丢失。
 */

#include "data_cache.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief 缓存上下文 */
typedef struct {
  cache_entry_t entries[CACHE_MAX_ENTRIES]; /**< 环形缓冲区 */
  int head;                                 /**< 头指针 */
  int tail;                                 /**< 尾指针 */
  int count;                                /**< 当前条目数 */
  pthread_mutex_t lock;                     /**< 互斥锁 */
  int initialized;                          /**< 初始化标志 */
} cache_context_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 缓存全局上下文 */
static cache_context_t g_cache = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .initialized = 0,
};

/** @brief 缓存统计信息 */
static cache_stats_t g_stats = {
    .total_push_count = 0,
    .total_pop_count = 0,
    .compression_enabled = CACHE_ENABLE_COMPRESSION,
    .original_bytes = 0,
    .compressed_bytes = 0,
    .compression_ratio = 1.0f,
};

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

cache_error_t data_cache_init(void) {
  pthread_mutex_lock(&g_cache.lock);

  if (g_cache.initialized) {
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_OK;
  }

  /* 确保缓存目录存在 */
  mkdir("/etc/device", 0700);

  /* 尝试从文件加载缓存 */
  data_cache_load_from_file();

  g_cache.initialized = 1;
  pthread_mutex_unlock(&g_cache.lock);

  printf("[CACHE] Initialized with %d entries\n", g_cache.count);
  return CACHE_OK;
}

cache_error_t data_cache_push(const char *data, int data_len) {
  if (!data || data_len <= 0 || data_len >= CACHE_ENTRY_MAX_LEN) {
    return CACHE_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_cache.lock);

  if (!g_cache.initialized) {
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_ERROR;
  }

  /* 检查缓存是否已满 */
  if (g_cache.count >= CACHE_MAX_ENTRIES) {
    /* 缓存已满，覆盖最旧的数据 */
    printf("[CACHE] Cache full, overwriting oldest entry\n");
    g_cache.head = (g_cache.head + 1) % CACHE_MAX_ENTRIES;
    g_cache.count--;
  }

  /* 添加新条目 */
  cache_entry_t *entry = &g_cache.entries[g_cache.tail];
  memcpy(entry->data, data, data_len);
  entry->data[data_len] = '\0';
  entry->data_len = data_len;
  entry->timestamp = time(NULL);

  g_cache.tail = (g_cache.tail + 1) % CACHE_MAX_ENTRIES;
  g_cache.count++;

  /* 更新统计信息 */
  g_stats.total_push_count++;
  g_stats.original_bytes += data_len;
  g_stats.compressed_bytes += data_len; /* 简化处理，实际压缩需实现压缩算法 */

  pthread_mutex_unlock(&g_cache.lock);

  printf("[CACHE] Pushed entry, count=%d\n", g_cache.count);
  return CACHE_OK;
}

cache_error_t data_cache_pop(char *data, int max_len, int *data_len) {
  if (!data || max_len <= 0 || !data_len) {
    return CACHE_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_cache.lock);

  if (!g_cache.initialized) {
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_ERROR;
  }

  /* 检查缓存是否为空 */
  if (g_cache.count <= 0) {
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_ERROR_EMPTY;
  }

  /* 取出最旧的条目 */
  cache_entry_t *entry = &g_cache.entries[g_cache.head];
  int copy_len = entry->data_len;
  if (copy_len >= max_len) {
    copy_len = max_len - 1;
  }

  memcpy(data, entry->data, copy_len);
  data[copy_len] = '\0';
  *data_len = copy_len;

  g_cache.head = (g_cache.head + 1) % CACHE_MAX_ENTRIES;
  g_cache.count--;

  /* 更新统计信息 */
  g_stats.total_pop_count++;

  pthread_mutex_unlock(&g_cache.lock);

  printf("[CACHE] Popped entry, count=%d\n", g_cache.count);
  return CACHE_OK;
}

int data_cache_count(void) {
  pthread_mutex_lock(&g_cache.lock);
  int count = g_cache.count;
  pthread_mutex_unlock(&g_cache.lock);
  return count;
}

bool data_cache_is_empty(void) {
  pthread_mutex_lock(&g_cache.lock);
  bool empty = (g_cache.count <= 0);
  pthread_mutex_unlock(&g_cache.lock);
  return empty;
}

bool data_cache_is_full(void) {
  pthread_mutex_lock(&g_cache.lock);
  bool full = (g_cache.count >= CACHE_MAX_ENTRIES);
  pthread_mutex_unlock(&g_cache.lock);
  return full;
}

cache_error_t data_cache_save_to_file(void) {
  pthread_mutex_lock(&g_cache.lock);

  FILE *fp = fopen(CACHE_FILE_PATH, "wb");
  if (!fp) {
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_ERROR_IO;
  }

  /* 写入缓存头部信息 */
  int magic = 0x43414348; /* "CACHE" */
  if (fwrite(&magic, sizeof(magic), 1, fp) != 1 ||
      fwrite(&g_cache.count, sizeof(g_cache.count), 1, fp) != 1 ||
      fwrite(&g_cache.head, sizeof(g_cache.head), 1, fp) != 1 ||
      fwrite(&g_cache.tail, sizeof(g_cache.tail), 1, fp) != 1) {
    fclose(fp);
    pthread_mutex_unlock(&g_cache.lock);
    return CACHE_ERROR_IO;
  }

  /* 写入所有条目 */
  for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
    if (fwrite(&g_cache.entries[i], sizeof(cache_entry_t), 1, fp) != 1) {
      fclose(fp);
      pthread_mutex_unlock(&g_cache.lock);
      return CACHE_ERROR_IO;
    }
  }

  fclose(fp);
  pthread_mutex_unlock(&g_cache.lock);

  printf("[CACHE] Saved %d entries to file\n", g_cache.count);
  return CACHE_OK;
}

cache_error_t data_cache_load_from_file(void) {
  FILE *fp = fopen(CACHE_FILE_PATH, "rb");
  if (!fp) {
    printf("[CACHE] No cache file found\n");
    return CACHE_OK; /* 文件不存在不是错误 */
  }

  /* 读取缓存头部信息 */
  int magic;
  if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != 0x43414348) {
    printf("[CACHE] Invalid cache file format\n");
    fclose(fp);
    return CACHE_ERROR_IO;
  }

  if (fread(&g_cache.count, sizeof(g_cache.count), 1, fp) != 1 ||
      fread(&g_cache.head, sizeof(g_cache.head), 1, fp) != 1 ||
      fread(&g_cache.tail, sizeof(g_cache.tail), 1, fp) != 1) {
    printf("[CACHE] Failed to read cache header\n");
    fclose(fp);
    return CACHE_ERROR_IO;
  }

  /* 读取所有条目 */
  for (int i = 0; i < CACHE_MAX_ENTRIES; i++) {
    if (fread(&g_cache.entries[i], sizeof(cache_entry_t), 1, fp) != 1) {
      printf("[CACHE] Failed to read cache entry %d\n", i);
      fclose(fp);
      return CACHE_ERROR_IO;
    }
  }

  fclose(fp);

  /* 加载成功后删除缓存文件，避免重复加载 */
  remove(CACHE_FILE_PATH);

  printf("[CACHE] Loaded %d entries from file\n", g_cache.count);
  return CACHE_OK;
}

void data_cache_clear(void) {
  pthread_mutex_lock(&g_cache.lock);

  g_cache.head = 0;
  g_cache.tail = 0;
  g_cache.count = 0;

  pthread_mutex_unlock(&g_cache.lock);

  /* 删除缓存文件 */
  remove(CACHE_FILE_PATH);

  printf("[CACHE] Cache cleared\n");
}

const char *data_cache_get_error_string(cache_error_t error) {
  switch (error) {
  case CACHE_OK:
    return "Success";
  case CACHE_ERROR:
    return "Generic error";
  case CACHE_ERROR_FULL:
    return "Cache full";
  case CACHE_ERROR_EMPTY:
    return "Cache empty";
  case CACHE_ERROR_IO:
    return "IO error";
  case CACHE_ERROR_PARAM:
    return "Invalid parameter";
  default:
    return "Unknown error";
  }
}

void data_cache_cleanup(void) {
  /* 保存缓存到文件 */
  data_cache_save_to_file();

  pthread_mutex_destroy(&g_cache.lock);
  g_cache.initialized = 0;

  printf("[CACHE] Cleanup completed\n");
}

/* ========================================================================== */
/*                              压缩功能实现 */
/* ========================================================================== */

cache_error_t data_cache_get_stats(cache_stats_t *stats) {
  if (!stats) {
    return CACHE_ERROR_PARAM;
  }

  pthread_mutex_lock(&g_cache.lock);

  stats->total_push_count = g_stats.total_push_count;
  stats->total_pop_count = g_stats.total_pop_count;
  stats->compression_enabled = g_stats.compression_enabled;
  stats->original_bytes = g_stats.original_bytes;
  stats->compressed_bytes = g_stats.compressed_bytes;

  /* 计算压缩率 */
  if (g_stats.original_bytes > 0) {
    stats->compression_ratio = (float)g_stats.compressed_bytes / g_stats.original_bytes;
  } else {
    stats->compression_ratio = 1.0f;
  }

  pthread_mutex_unlock(&g_cache.lock);
  return CACHE_OK;
}

void data_cache_reset_stats(void) {
  pthread_mutex_lock(&g_cache.lock);

  g_stats.total_push_count = 0;
  g_stats.total_pop_count = 0;
  g_stats.original_bytes = 0;
  g_stats.compressed_bytes = 0;
  g_stats.compression_ratio = 1.0f;

  pthread_mutex_unlock(&g_cache.lock);
}

void data_cache_set_compression(int enable) {
  pthread_mutex_lock(&g_cache.lock);
  g_stats.compression_enabled = enable ? 1 : 0;
  pthread_mutex_unlock(&g_cache.lock);
  printf("[CACHE] Compression %s\n", enable ? "enabled" : "disabled");
}

int data_cache_get_compression(void) {
  pthread_mutex_lock(&g_cache.lock);
  int enabled = g_stats.compression_enabled;
  pthread_mutex_unlock(&g_cache.lock);
  return enabled;
}
