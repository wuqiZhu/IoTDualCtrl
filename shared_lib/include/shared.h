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

/* 仅包含 shared_lib 实际提供的模块 */
#include "cJSON.h"       /* JSON解析库 */
#include "watchdog.h"    /* 软件看门狗 */
#include "rpc.h"         /* RPC端口定义（统一入口） */

/*
 * 注意：以下模块的头文件位于 lesson6/ 目录，不属于 shared_lib：
 *   error.h / log.h / config.h / data_cache.h / system_monitor.h
 *   security_audit.h / crypto_utils.h / memory_pool.h / perf_monitor.h
 *   device_discovery.h
 * 如需在外部使用这些模块，请直接包含 lesson6/ 下的对应头文件。
 */

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
