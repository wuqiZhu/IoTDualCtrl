/**
 * @file memory_pool.c
 * @brief 内存池管理模块实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现内存池管理和泄漏检测功能，适合嵌入式ARM平台。
 */

#include "memory_pool.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*                              内存池内部结构 */
/* ========================================================================== */

/** @brief 空闲块节点 */
typedef struct free_node {
  struct free_node *next;
} free_node_t;

/** @brief 内存池结构 */
typedef struct {
  bool active;                /**< 是否激活 */
  size_t block_size;          /**< 块大小（含对齐） */
  int total_blocks;           /**< 总块数 */
  int free_blocks;            /**< 空闲块数 */
  void *memory;               /**< 内存池起始地址 */
  free_node_t *free_list;     /**< 空闲链表 */
  pthread_mutex_t lock;       /**< 互斥锁 */
} pool_t;

/* ========================================================================== */
/*                              全局变量 */
/* ========================================================================== */

/** @brief 内存池数组 */
static pool_t g_pools[MEMPOOL_MAX_POOLS];

/** @brief 内存跟踪数组 */
static mem_track_t g_tracks[MEMPOOL_MAX_TRACKS];

/** @brief 内存统计 */
static mem_stats_t g_mem_stats;

/** @brief 内存跟踪互斥锁 */
static pthread_mutex_t g_track_lock = PTHREAD_MUTEX_INITIALIZER;

/** @brief 内存池互斥锁 */
static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;

/** @brief 告警回调 */
static mem_alert_cb_t g_alert_callback = NULL;

/** @brief 初始化标志 */
static bool g_track_initialized = false;
static bool g_pools_initialized = false;

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

static void init_pools_if_needed(void) {
  if (!g_pools_initialized) {
    memset(g_pools, 0, sizeof(g_pools));
    g_pools_initialized = true;
  }
}

static void check_memory_threshold(void) {
  if (!g_alert_callback) return;

  for (int i = 0; i < MEMPOOL_MAX_POOLS; i++) {
    if (!g_pools[i].active) continue;

    size_t total = (size_t)g_pools[i].total_blocks * g_pools[i].block_size;
    size_t used = (size_t)(g_pools[i].total_blocks - g_pools[i].free_blocks) * g_pools[i].block_size;
    int percent = total > 0 ? (int)(used * 100 / total) : 0;

    if (percent >= MEMPOOL_CRIT_THRESHOLD) {
      g_alert_callback(percent, used, total);
    } else if (percent >= MEMPOOL_WARN_THRESHOLD) {
      g_alert_callback(percent, used, total);
    }
  }
}

/* ========================================================================== */
/*                              内存跟踪实现 */
/* ========================================================================== */

mempool_error_t mem_track_init(void) {
  pthread_mutex_lock(&g_track_lock);

  if (g_track_initialized) {
    pthread_mutex_unlock(&g_track_lock);
    return MEMPOOL_OK;
  }

  memset(g_tracks, 0, sizeof(g_tracks));
  memset(&g_mem_stats, 0, sizeof(g_mem_stats));
  g_track_initialized = true;

  pthread_mutex_unlock(&g_track_lock);

  printf("[MEMPOOL] Memory tracking initialized (max %d entries)\n", MEMPOOL_MAX_TRACKS);
  return MEMPOOL_OK;
}

void mem_track_cleanup(void) {
  pthread_mutex_lock(&g_track_lock);

  if (g_track_initialized) {
    int leaks = 0;
    for (int i = 0; i < MEMPOOL_MAX_TRACKS; i++) {
      if (g_tracks[i].active) {
        leaks++;
      }
    }
    if (leaks > 0) {
      printf("[MEMPOOL] WARNING: %d memory leaks detected at cleanup\n", leaks);
    }
    g_track_initialized = false;
  }

  pthread_mutex_unlock(&g_track_lock);
}

mempool_error_t mem_track_alloc(void *ptr, size_t size,
                                const char *file, int line, const char *func) {
  if (!ptr) return MEMPOOL_ERROR_PARAM;

  pthread_mutex_lock(&g_track_lock);

  if (!g_track_initialized) {
    pthread_mutex_unlock(&g_track_lock);
    return MEMPOOL_ERROR;
  }

  int slot = -1;
  for (int i = 0; i < MEMPOOL_MAX_TRACKS; i++) {
    if (!g_tracks[i].active) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    pthread_mutex_unlock(&g_track_lock);
    return MEMPOOL_ERROR_TRACK;
  }

  g_tracks[slot].ptr = ptr;
  g_tracks[slot].size = size;
  g_tracks[slot].file = file;
  g_tracks[slot].line = line;
  g_tracks[slot].func = func;
  g_tracks[slot].active = true;

  g_mem_stats.total_allocated += size;
  g_mem_stats.current_usage += size;
  g_mem_stats.alloc_count++;
  g_mem_stats.active_allocs++;

  if (g_mem_stats.current_usage > g_mem_stats.peak_usage) {
    g_mem_stats.peak_usage = g_mem_stats.current_usage;
  }

  pthread_mutex_unlock(&g_track_lock);
  return MEMPOOL_OK;
}

mempool_error_t mem_track_free(void *ptr) {
  if (!ptr) return MEMPOOL_ERROR_PARAM;

  pthread_mutex_lock(&g_track_lock);

  if (!g_track_initialized) {
    pthread_mutex_unlock(&g_track_lock);
    return MEMPOOL_ERROR;
  }

  for (int i = 0; i < MEMPOOL_MAX_TRACKS; i++) {
    if (g_tracks[i].active && g_tracks[i].ptr == ptr) {
      g_mem_stats.total_freed += g_tracks[i].size;
      g_mem_stats.current_usage -= g_tracks[i].size;
      g_mem_stats.free_count++;
      g_mem_stats.active_allocs--;

      g_tracks[i].active = false;
      g_tracks[i].ptr = NULL;
      g_tracks[i].size = 0;

      pthread_mutex_unlock(&g_track_lock);
      return MEMPOOL_OK;
    }
  }

  pthread_mutex_unlock(&g_track_lock);
  return MEMPOOL_ERROR;
}

int mem_track_detect_leaks(void) {
  int count = 0;

  pthread_mutex_lock(&g_track_lock);

  for (int i = 0; i < MEMPOOL_MAX_TRACKS; i++) {
    if (g_tracks[i].active) {
      count++;
    }
  }

  g_mem_stats.leak_count = count;
  pthread_mutex_unlock(&g_track_lock);

  return count;
}

void mem_track_print_leaks(void) {
  pthread_mutex_lock(&g_track_lock);

  int count = 0;
  printf("\n=== Memory Leak Report ===\n");

  for (int i = 0; i < MEMPOOL_MAX_TRACKS; i++) {
    if (g_tracks[i].active) {
      printf("  LEAK: %p (%zu bytes) at %s:%d in %s()\n",
             g_tracks[i].ptr, g_tracks[i].size,
             g_tracks[i].file ? g_tracks[i].file : "unknown",
             g_tracks[i].line,
             g_tracks[i].func ? g_tracks[i].func : "unknown");
      count++;
    }
  }

  if (count == 0) {
    printf("  No memory leaks detected.\n");
  } else {
    printf("  Total: %d leaks found\n", count);
  }
  printf("==========================\n\n");

  pthread_mutex_unlock(&g_track_lock);
}

mempool_error_t mem_track_get_stats(mem_stats_t *stats) {
  if (!stats) return MEMPOOL_ERROR_PARAM;

  pthread_mutex_lock(&g_track_lock);
  memcpy(stats, &g_mem_stats, sizeof(mem_stats_t));
  pthread_mutex_unlock(&g_track_lock);

  return MEMPOOL_OK;
}

void mem_track_print_stats(void) {
  pthread_mutex_lock(&g_track_lock);

  printf("\n=== Memory Statistics ===\n");
  printf("  Total Allocated : %10zu bytes (%zu KB)\n",
         g_mem_stats.total_allocated, g_mem_stats.total_allocated / 1024);
  printf("  Total Freed     : %10zu bytes (%zu KB)\n",
         g_mem_stats.total_freed, g_mem_stats.total_freed / 1024);
  printf("  Current Usage   : %10zu bytes (%zu KB)\n",
         g_mem_stats.current_usage, g_mem_stats.current_usage / 1024);
  printf("  Peak Usage      : %10zu bytes (%zu KB)\n",
         g_mem_stats.peak_usage, g_mem_stats.peak_usage / 1024);
  printf("  Alloc Count     : %10lu\n", g_mem_stats.alloc_count);
  printf("  Free Count      : %10lu\n", g_mem_stats.free_count);
  printf("  Active Allocs   : %10d\n", g_mem_stats.active_allocs);
  printf("  Leak Count      : %10d\n", g_mem_stats.leak_count);
  printf("=========================\n\n");

  pthread_mutex_unlock(&g_track_lock);
}

/* ========================================================================== */
/*                              跟踪版本的malloc/free */
/* ========================================================================== */

#ifdef MEMPOOL_TRACK_ENABLED

void *mem_track_malloc_impl(size_t size, const char *file, int line, const char *func) {
  void *ptr = malloc(size);
  if (ptr) {
    mem_track_alloc(ptr, size, file, line, func);
  }
  return ptr;
}

void *mem_track_calloc_impl(size_t nmemb, size_t size,
                            const char *file, int line, const char *func) {
  void *ptr = calloc(nmemb, size);
  if (ptr) {
    mem_track_alloc(ptr, nmemb * size, file, line, func);
  }
  return ptr;
}

void *mem_track_realloc_impl(void *ptr, size_t size,
                             const char *file, int line, const char *func) {
  if (ptr) {
    mem_track_free(ptr);
  }
  void *new_ptr = realloc(ptr, size);
  if (new_ptr) {
    mem_track_alloc(new_ptr, size, file, line, func);
  }
  return new_ptr;
}

void mem_track_free_impl(void *ptr) {
  if (ptr) {
    mem_track_free(ptr);
    free(ptr);
  }
}

#endif /* MEMPOOL_TRACK_ENABLED */

/* ========================================================================== */
/*                              固定大小内存池实现 */
/* ========================================================================== */

int mempool_create(size_t block_size, int block_count) {
  if (block_size < sizeof(free_node_t) || block_count <= 0) {
    return -1;
  }

  init_pools_if_needed();
  pthread_mutex_lock(&g_pool_lock);

  int slot = -1;
  for (int i = 0; i < MEMPOOL_MAX_POOLS; i++) {
    if (!g_pools[i].active) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    pthread_mutex_unlock(&g_pool_lock);
    printf("[MEMPOOL] ERROR: Maximum pool count reached\n");
    return -1;
  }

  size_t aligned_size = (block_size + 7) & ~(size_t)7;

  pool_t *pool = &g_pools[slot];
  pool->memory = malloc(aligned_size * (size_t)block_count);
  if (!pool->memory) {
    pthread_mutex_unlock(&g_pool_lock);
    printf("[MEMPOOL] ERROR: Failed to allocate pool memory (%zu bytes)\n",
           aligned_size * (size_t)block_count);
    return -1;
  }

  pool->block_size = aligned_size;
  pool->total_blocks = block_count;
  pool->free_blocks = block_count;
  pool->free_list = NULL;
  pool->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

  for (int i = block_count - 1; i >= 0; i--) {
    free_node_t *node = (free_node_t *)((char *)pool->memory + (size_t)i * aligned_size);
    node->next = pool->free_list;
    pool->free_list = node;
  }

  pool->active = true;
  pthread_mutex_unlock(&g_pool_lock);

  printf("[MEMPOOL] Pool %d created: block_size=%zu, blocks=%d, total=%zu KB\n",
         slot, aligned_size, block_count,
         (aligned_size * (size_t)block_count) / 1024);

  return slot;
}

mempool_error_t mempool_destroy(int pool_id) {
  if (pool_id < 0 || pool_id >= MEMPOOL_MAX_POOLS) return MEMPOOL_ERROR_PARAM;

  pthread_mutex_lock(&g_pool_lock);

  pool_t *pool = &g_pools[pool_id];
  if (!pool->active) {
    pthread_mutex_unlock(&g_pool_lock);
    return MEMPOOL_ERROR;
  }

  int leaked = pool->total_blocks - pool->free_blocks;
  if (leaked > 0) {
    printf("[MEMPOOL] WARNING: Pool %d has %d unfreed blocks\n", pool_id, leaked);
  }

  free(pool->memory);
  pool->active = false;
  pool->memory = NULL;
  pool->free_list = NULL;

  pthread_mutex_unlock(&g_pool_lock);

  printf("[MEMPOOL] Pool %d destroyed\n", pool_id);
  return MEMPOOL_OK;
}

void *mempool_alloc(int pool_id) {
  if (pool_id < 0 || pool_id >= MEMPOOL_MAX_POOLS) return NULL;

  pool_t *pool = &g_pools[pool_id];
  if (!pool->active) return NULL;

  pthread_mutex_lock(&pool->lock);

  if (!pool->free_list) {
    pthread_mutex_unlock(&pool->lock);
    return NULL;
  }

  free_node_t *node = pool->free_list;
  pool->free_list = node->next;
  pool->free_blocks--;

  pthread_mutex_unlock(&pool->lock);

  check_memory_threshold();
  return (void *)node;
}

mempool_error_t mempool_free(int pool_id, void *ptr) {
  if (pool_id < 0 || pool_id >= MEMPOOL_MAX_POOLS || !ptr) {
    return MEMPOOL_ERROR_PARAM;
  }

  pool_t *pool = &g_pools[pool_id];
  if (!pool->active) return MEMPOOL_ERROR;

  char *base = (char *)pool->memory;
  char *p = (char *)ptr;
  size_t total_size = pool->block_size * (size_t)pool->total_blocks;

  if (p < base || p >= base + total_size) {
    return MEMPOOL_ERROR_PARAM;
  }

  if ((size_t)(p - base) % pool->block_size != 0) {
    return MEMPOOL_ERROR_PARAM;
  }

  pthread_mutex_lock(&pool->lock);

  free_node_t *node = (free_node_t *)ptr;
  node->next = pool->free_list;
  pool->free_list = node;
  pool->free_blocks++;

  pthread_mutex_unlock(&pool->lock);

  return MEMPOOL_OK;
}

mempool_error_t mempool_get_info(int pool_id, pool_info_t *info) {
  if (pool_id < 0 || pool_id >= MEMPOOL_MAX_POOLS || !info) {
    return MEMPOOL_ERROR_PARAM;
  }

  pool_t *pool = &g_pools[pool_id];
  if (!pool->active) return MEMPOOL_ERROR;

  pthread_mutex_lock(&pool->lock);

  info->pool_id = pool_id;
  info->block_size = pool->block_size;
  info->total_blocks = pool->total_blocks;
  info->free_blocks = pool->free_blocks;
  info->used_blocks = pool->total_blocks - pool->free_blocks;
  info->total_memory = pool->block_size * (size_t)pool->total_blocks;
  info->used_memory = pool->block_size * (size_t)(pool->total_blocks - pool->free_blocks);

  pthread_mutex_unlock(&pool->lock);

  return MEMPOOL_OK;
}

void mempool_print_status(void) {
  init_pools_if_needed();

  printf("\n=== Memory Pool Status ===\n");
  printf("%-4s %-10s %-8s %-8s %-8s %-10s %-10s\n",
         "ID", "BlockSize", "Total", "Free", "Used", "TotalKB", "UsedKB");
  printf("---- ---------- -------- -------- -------- ---------- ----------\n");

  for (int i = 0; i < MEMPOOL_MAX_POOLS; i++) {
    if (!g_pools[i].active) continue;

    pool_info_t info;
    if (mempool_get_info(i, &info) == MEMPOOL_OK) {
      printf("%-4d %-10zu %-8d %-8d %-8d %-10zu %-10zu\n",
             info.pool_id, info.block_size, info.total_blocks,
             info.free_blocks, info.used_blocks,
             info.total_memory / 1024, info.used_memory / 1024);
    }
  }
  printf("===========================\n\n");
}

void mempool_set_alert_callback(mem_alert_cb_t callback) {
  g_alert_callback = callback;
}

/* ========================================================================== */
/*                              内存使用监控实现 */
/* ========================================================================== */

mempool_error_t mem_monitor_get_usage(unsigned long *vm_rss_kb, unsigned long *vm_size_kb) {
  if (!vm_rss_kb && !vm_size_kb) return MEMPOOL_ERROR_PARAM;

  FILE *fp = fopen("/proc/self/status", "r");
  if (!fp) return MEMPOOL_ERROR;

  char line[256];
  unsigned long rss = 0, vsize = 0;

  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      sscanf(line + 6, "%lu", &rss);
    } else if (strncmp(line, "VmSize:", 7) == 0) {
      sscanf(line + 7, "%lu", &vsize);
    }
  }

  fclose(fp);

  if (vm_rss_kb) *vm_rss_kb = rss;
  if (vm_size_kb) *vm_size_kb = vsize;

  return MEMPOOL_OK;
}

mempool_error_t mem_monitor_get_system(unsigned long *total_kb, unsigned long *free_kb) {
  if (!total_kb && !free_kb) return MEMPOOL_ERROR_PARAM;

  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp) return MEMPOOL_ERROR;

  char line[256];
  unsigned long total = 0, free = 0;

  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "MemTotal:", 9) == 0) {
      sscanf(line + 9, "%lu", &total);
    } else if (strncmp(line, "MemFree:", 8) == 0) {
      sscanf(line + 8, "%lu", &free);
    }
  }

  fclose(fp);

  if (total_kb) *total_kb = total;
  if (free_kb) *free_kb = free;

  return MEMPOOL_OK;
}

void mem_monitor_print_report(void) {
  unsigned long rss = 0, vsize = 0, sys_total = 0, sys_free = 0;

  printf("\n=== Memory Monitor Report ===\n");

  if (mem_monitor_get_usage(&rss, &vsize) == MEMPOOL_OK) {
    printf("  Process RSS     : %8lu KB (%lu MB)\n", rss, rss / 1024);
    printf("  Process VSize   : %8lu KB (%lu MB)\n", vsize, vsize / 1024);
  } else {
    printf("  Process memory info unavailable\n");
  }

  if (mem_monitor_get_system(&sys_total, &sys_free) == MEMPOOL_OK) {
    unsigned long used = sys_total - sys_free;
    int percent = sys_total > 0 ? (int)(used * 100 / sys_total) : 0;
    printf("  System Total    : %8lu KB (%lu MB)\n", sys_total, sys_total / 1024);
    printf("  System Free     : %8lu KB (%lu MB)\n", sys_free, sys_free / 1024);
    printf("  System Used     : %8lu KB (%lu MB) [%d%%]\n", used, used / 1024, percent);
  } else {
    printf("  System memory info unavailable\n");
  }

  printf("=============================\n\n");
}
