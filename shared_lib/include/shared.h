/**
 * @file shared.h
 * @brief 共享库统一头文件
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 包含此头文件即可使用共享库的所有功能。
 */

#ifndef SHARED_H
#define SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              版本信息 */
/* ========================================================================== */

#define SHARED_LIB_VERSION_MAJOR 1
#define SHARED_LIB_VERSION_MINOR 0
#define SHARED_LIB_VERSION_PATCH 0
#define SHARED_LIB_VERSION "1.0.0"

/* ========================================================================== */
/*                              模块头文件包含 */
/* ========================================================================== */

/* cJSON - JSON解析库 */
#include "cJSON.h"

/* 错误处理 */
#include "error.h"

/* 日志模块 */
#include "log.h"

/* 看门狗模块 */
#include "watchdog.h"

/* 配置管理 */
#include "config.h"

/* 数据缓存 */
#include "data_cache.h"

/* 系统监控 */
#include "system_monitor.h"

/* 安全审计 */
#include "security_audit.h"

/* 数据安全 */
#include "crypto_utils.h"

/* 内存池管理 */
#include "memory_pool.h"

/* 性能监控 */
#include "perf_monitor.h"

/* 设备发现 */
#include "device_discovery.h"

/* ========================================================================== */
/*                              公共工具函数 */
/* ========================================================================== */

/**
 * @brief 获取共享库版本
 * @return 版本字符串
 */
const char *shared_get_version(void);

/**
 * @brief 打印共享库信息
 */
void shared_print_info(void);

/**
 * @brief 初始化共享库
 * @return 0成功, -1失败
 */
int shared_init(void);

/**
 * @brief 清理共享库资源
 */
void shared_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_H */
