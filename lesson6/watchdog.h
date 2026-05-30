/**
 * @file watchdog.h
 * @brief 软件看门狗模块接口定义
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 提供软件看门狗功能，防止程序卡死导致系统无响应。
 * 主程序定期喂狗，看门狗线程监控喂狗间隔，超时则触发重启。
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 默认看门狗超时时间（秒） */
#define WATCHDOG_DEFAULT_TIMEOUT_SEC 30

/** @brief 默认喂狗间隔（秒），应小于超时时间 */
#define WATCHDOG_DEFAULT_FEED_INTERVAL_SEC 10

/** @brief 最大看门狗超时时间（秒） */
#define WATCHDOG_MAX_TIMEOUT_SEC 300

/** @brief 最小看门狗超时时间（秒） */
#define WATCHDOG_MIN_TIMEOUT_SEC 5

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 看门狗错误码 */
typedef enum {
  WATCHDOG_OK = 0,               /**< 操作成功 */
  WATCHDOG_ERROR = -1,           /**< 通用错误 */
  WATCHDOG_ERROR_INVALID_PARAM = -2, /**< 无效参数 */
  WATCHDOG_ERROR_ALREADY_STARTED = -3, /**< 看门狗已启动 */
  WATCHDOG_ERROR_NOT_STARTED = -4, /**< 看门狗未启动 */
  WATCHDOG_ERROR_THREAD_CREATE = -5, /**< 线程创建失败 */
  WATCHDOG_ERROR_TIMEOUT = -6,   /**< 看门狗超时 */
} watchdog_error_t;

/* ========================================================================== */
/*                              回调函数类型 */
/* ========================================================================== */

/**
 * @brief 看门狗超时回调函数类型
 * @param user_data 用户自定义数据
 *
 * 当看门狗检测到超时时调用此函数。
 * 建议在此函数中执行清理操作，然后退出程序或重启服务。
 */
typedef void (*watchdog_timeout_callback_t)(void *user_data);

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化看门狗模块
 * @param timeout_sec 超时时间（秒），0使用默认值
 * @param callback 超时回调函数，NULL使用默认处理（直接退出）
 * @param user_data 传递给回调函数的用户数据
 * @return WATCHDOG_OK成功
 */
watchdog_error_t watchdog_init(unsigned int timeout_sec,
                               watchdog_timeout_callback_t callback,
                               void *user_data);

/**
 * @brief 启动看门狗
 * @return WATCHDOG_OK成功
 *
 * 启动看门狗线程，开始监控主程序状态。
 * 调用此函数后，必须定期调用 watchdog_feed() 喂狗。
 */
watchdog_error_t watchdog_start(void);

/**
 * @brief 停止看门狗
 * @return WATCHDOG_OK成功
 *
 * 停止看门狗线程，停止监控。
 */
watchdog_error_t watchdog_stop(void);

/**
 * @brief 喂狗（重置看门狗计时器）
 * @return WATCHDOG_OK成功
 *
 * 主程序定期调用此函数，表明程序正常运行。
 * 如果超过 watchdog_init() 设置的超时时间未调用此函数，
 * 看门狗将触发超时处理。
 */
watchdog_error_t watchdog_feed(void);

/**
 * @brief 检查看门狗是否已启动
 * @return true已启动，false未启动
 */
bool watchdog_is_running(void);

/**
 * @brief 获取看门狗错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *watchdog_get_error_string(watchdog_error_t error);

/**
 * @brief 清理看门狗模块资源
 *
 * 在程序退出前调用，释放看门狗模块占用的资源。
 */
void watchdog_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_H */
