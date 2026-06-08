/**
 * @file memory_pool.h
 * @brief 内存池管理模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供内存管理功能，包括：
 * - 固定大小内存池（减少碎片）
 * - 内存泄漏检测（分配/释放跟踪）
 * - 内存使用统计
 * - 内存使用告警
 */

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 最大内存池数量 */
#define MEMPOOL_MAX_POOLS 8

/** @brief 最大内存跟踪记录数 */
#define MEMPOOL_MAX_TRACKS 256

/** @brief 内存使用告警阈值（百分比） */
#define MEMPOOL_WARN_THRESHOLD 80

/** @brief 内存使用严重告警阈值（百分比） */
#define MEMPOOL_CRIT_THRESHOLD 95

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 内存池错误码 */
typedef enum {
  MEMPOOL_OK = 0,             /**< 操作成功 */
  MEMPOOL_ERROR = -1,         /**< 通用错误 */
  MEMPOOL_ERROR_PARAM = -2,   /**< 参数错误 */
  MEMPOOL_ERROR_FULL = -3,    /**< 内存池已满 */
  MEMPOOL_ERROR_EMPTY = -4,   /**< 内存池为空 */
  MEMPOOL_ERROR_OOM = -5,     /**< 内存不足 */
  MEMPOOL_ERROR_TRACK = -6,   /**< 跟踪表满 */
} mempool_error_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief 内存分配跟踪信息 */
typedef struct {
  void *ptr;                  /**< 分配的指针 */
  size_t size;                /**< 分配的大小 */
  const char *file;           /**< 分配所在的文件 */
  int line;                   /**< 分配所在的行号 */
  const char *func;           /**< 分配所在的函数 */
  bool active;                /**< 是否活跃（未释放） */
} mem_track_t;

/** @brief 内存使用统计 */
typedef struct {
  size_t total_allocated;     /**< 总分配内存（字节） */
  size_t total_freed;         /**< 总释放内存（字节） */
  size_t current_usage;       /**< 当前使用内存（字节） */
  size_t peak_usage;          /**< 峰值使用内存（字节） */
  unsigned long alloc_count;  /**< 分配次数 */
  unsigned long free_count;   /**< 释放次数 */
  int active_allocs;          /**< 当前活跃分配数 */
  int leak_count;             /**< 泄漏检测数量 */
} mem_stats_t;

/** @brief 内存池信息 */
typedef struct {
  int pool_id;                /**< 池ID */
  size_t block_size;          /**< 块大小 */
  int total_blocks;           /**< 总块数 */
  int free_blocks;            /**< 空闲块数 */
  int used_blocks;            /**< 已用块数 */
  size_t total_memory;        /**< 总内存 */
  size_t used_memory;         /**< 已用内存 */
} pool_info_t;

/** @brief 内存告警回调类型 */
typedef void (*mem_alert_cb_t)(int threshold_percent, size_t current_usage, size_t total);

/* ========================================================================== */
/*                              内存跟踪接口（调试用） */
/* ========================================================================== */

/**
 * @brief 初始化内存跟踪模块
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_track_init(void);

/**
 * @brief 清理内存跟踪模块
 */
void mem_track_cleanup(void);

/**
 * @brief 记录内存分配（内部使用，用宏代替）
 * @param ptr 分配的指针
 * @param size 分配的大小
 * @param file 源文件名
 * @param line 行号
 * @param func 函数名
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_track_alloc(void *ptr, size_t size,
                                const char *file, int line, const char *func);

/**
 * @brief 记录内存释放（内部使用，用宏代替）
 * @param ptr 释放的指针
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_track_free(void *ptr);

/**
 * @brief 检测内存泄漏
 * @return 泄漏的分配数
 */
int mem_track_detect_leaks(void);

/**
 * @brief 打印内存泄漏报告
 */
void mem_track_print_leaks(void);

/**
 * @brief 获取内存统计信息
 * @param stats 输出统计信息
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_track_get_stats(mem_stats_t *stats);

/**
 * @brief 打印内存统计报告
 */
void mem_track_print_stats(void);

/* 调试模式下使用跟踪版本的malloc/free */
#ifdef MEMPOOL_TRACK_ENABLED
#define tracked_malloc(size) mem_track_malloc_impl(size, __FILE__, __LINE__, __func__)
#define tracked_calloc(nm, size) mem_track_calloc_impl(nm, size, __FILE__, __LINE__, __func__)
#define tracked_realloc(ptr, size) mem_track_realloc_impl(ptr, size, __FILE__, __LINE__, __func__)
#define tracked_free(ptr) mem_track_free_impl(ptr)
void *mem_track_malloc_impl(size_t size, const char *file, int line, const char *func);
void *mem_track_calloc_impl(size_t nmemb, size_t size, const char *file, int line, const char *func);
void *mem_track_realloc_impl(void *ptr, size_t size, const char *file, int line, const char *func);
void mem_track_free_impl(void *ptr);
#endif

/* ========================================================================== */
/*                              固定大小内存池接口 */
/* ========================================================================== */

/**
 * @brief 创建内存池
 * @param block_size 每个块的大小
 * @param block_count 块的数量
 * @return 池ID, -1失败
 */
int mempool_create(size_t block_size, int block_count);

/**
 * @brief 销毁内存池
 * @param pool_id 池ID
 * @return MEMPOOL_OK成功
 */
mempool_error_t mempool_destroy(int pool_id);

/**
 * @brief 从内存池分配一个块
 * @param pool_id 池ID
 * @return 分配的指针, NULL失败
 */
void *mempool_alloc(int pool_id);

/**
 * @brief 释放一个块回内存池
 * @param pool_id 池ID
 * @param ptr 块指针
 * @return MEMPOOL_OK成功
 */
mempool_error_t mempool_free(int pool_id, void *ptr);

/**
 * @brief 获取内存池信息
 * @param pool_id 池ID
 * @param info 输出信息
 * @return MEMPOOL_OK成功
 */
mempool_error_t mempool_get_info(int pool_id, pool_info_t *info);

/**
 * @brief 打印所有内存池状态
 */
void mempool_print_status(void);

/**
 * @brief 设置内存使用告警回调
 * @param callback 告警回调函数
 */
void mempool_set_alert_callback(mem_alert_cb_t callback);

/* ========================================================================== */
/*                              内存使用监控接口 */
/* ========================================================================== */

/**
 * @brief 获取当前进程内存使用（读取/proc/self/status）
 * @param vm_rss_kb 输出RSS内存（KB）
 * @param vm_size_kb 输出虚拟内存（KB）
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_monitor_get_usage(unsigned long *vm_rss_kb, unsigned long *vm_size_kb);

/**
 * @brief 获取系统可用内存
 * @param total_kb 输出总内存（KB）
 * @param free_kb 输出空闲内存（KB）
 * @return MEMPOOL_OK成功
 */
mempool_error_t mem_monitor_get_system(unsigned long *total_kb, unsigned long *free_kb);

/**
 * @brief 打印内存监控报告
 */
void mem_monitor_print_report(void);

#ifdef __cplusplus
}
#endif

#endif /* MEMORY_POOL_H */
