/**
 * @file data_cache.h
 * @brief 数据缓存模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 提供数据缓存功能，用于断网重传场景。
 * 使用环形缓冲区存储遥测数据，支持文件持久化。
 */

#ifndef DATA_CACHE_H
#define DATA_CACHE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 缓存最大条目数 */
#define CACHE_MAX_ENTRIES 100

/** @brief 单条数据最大长度 */
#define CACHE_ENTRY_MAX_LEN 512

/** @brief 缓存文件路径 */
#define CACHE_FILE_PATH "/tmp/telemetry_cache.dat"

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 数据缓存错误码 */
typedef enum {
  CACHE_OK = 0,               /**< 操作成功 */
  CACHE_ERROR = -1,           /**< 通用错误 */
  CACHE_ERROR_FULL = -2,      /**< 缓存已满 */
  CACHE_ERROR_EMPTY = -3,     /**< 缓存为空 */
  CACHE_ERROR_IO = -4,        /**< IO错误 */
  CACHE_ERROR_PARAM = -5,     /**< 参数错误 */
} cache_error_t;

/* ========================================================================== */
/*                              数据结构 */
/* ========================================================================== */

/** @brief 缓存条目 */
typedef struct {
  char data[CACHE_ENTRY_MAX_LEN]; /**< 数据内容 */
  int data_len;                    /**< 数据长度 */
  long timestamp;                  /**< 时间戳 */
} cache_entry_t;

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化数据缓存模块
 * @return CACHE_OK成功
 */
cache_error_t data_cache_init(void);

/**
 * @brief 添加数据到缓存
 * @param data 数据内容
 * @param data_len 数据长度
 * @return CACHE_OK成功, CACHE_ERROR_FULL缓存已满
 */
cache_error_t data_cache_push(const char *data, int data_len);

/**
 * @brief 从缓存取出数据
 * @param data 输出数据缓冲区
 * @param max_len 缓冲区最大长度
 * @param data_len 输出数据长度
 * @return CACHE_OK成功, CACHE_ERROR_EMPTY缓存为空
 */
cache_error_t data_cache_pop(char *data, int max_len, int *data_len);

/**
 * @brief 获取缓存中的数据条数
 * @return 缓存条目数
 */
int data_cache_count(void);

/**
 * @brief 检查缓存是否为空
 * @return true为空，false非空
 */
bool data_cache_is_empty(void);

/**
 * @brief 检查缓存是否已满
 * @return true已满，false未满
 */
bool data_cache_is_full(void);

/**
 * @brief 保存缓存到文件
 * @return CACHE_OK成功
 */
cache_error_t data_cache_save_to_file(void);

/**
 * @brief 从文件加载缓存
 * @return CACHE_OK成功
 */
cache_error_t data_cache_load_from_file(void);

/**
 * @brief 清空缓存
 */
void data_cache_clear(void);

/**
 * @brief 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *data_cache_get_error_string(cache_error_t error);

/**
 * @brief 清理缓存模块资源
 */
void data_cache_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_CACHE_H */
