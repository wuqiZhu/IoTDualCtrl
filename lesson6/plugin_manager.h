/**
 * @file plugin_manager.h
 * @brief 插件管理器接口定义
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 提供插件动态加载和管理功能，支持：
 * - 插件动态加载（dlopen/dlsym）
 * - 插件生命周期管理
 * - 插件接口注册
 * - 插件配置管理
 */

#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief 最大插件数量 */
#define PLUGIN_MAX_PLUGINS 16

/** @brief 插件名称最大长度 */
#define PLUGIN_MAX_NAME_LEN 64

/** @brief 插件版本最大长度 */
#define PLUGIN_MAX_VERSION_LEN 16

/** @brief 插件路径最大长度 */
#define PLUGIN_MAX_PATH_LEN 256

/** @brief 插件目录路径 */
#define PLUGIN_DIR "/etc/device/plugins"

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 插件管理器错误码 */
typedef enum {
  PLUGIN_OK = 0,               /**< 操作成功 */
  PLUGIN_ERROR = -1,           /**< 通用错误 */
  PLUGIN_ERROR_PARAM = -2,     /**< 参数错误 */
  PLUGIN_ERROR_NOT_FOUND = -3, /**< 插件未找到 */
  PLUGIN_ERROR_LOAD = -4,      /**< 加载失败 */
  PLUGIN_ERROR_INIT = -5,      /**< 初始化失败 */
  PLUGIN_ERROR_FULL = -6,      /**< 插件已满 */
  PLUGIN_ERROR_ALREADY = -7,   /**< 插件已存在 */
  PLUGIN_ERROR_SYMBOL = -8,    /**< 符号未找到 */
} plugin_error_t;

/* ========================================================================== */
/*                              插件状态定义 */
/* ========================================================================== */

/** @brief 插件状态 */
typedef enum {
  PLUGIN_STATE_UNLOADED = 0, /**< 未加载 */
  PLUGIN_STATE_LOADED,       /**< 已加载 */
  PLUGIN_STATE_INITIALIZED,  /**< 已初始化 */
  PLUGIN_STATE_STARTED,      /**< 已启动 */
  PLUGIN_STATE_STOPPED,      /**< 已停止 */
  PLUGIN_STATE_ERROR,        /**< 错误状态 */
} plugin_state_t;

/* ========================================================================== */
/*                              插件接口定义 */
/* ========================================================================== */

/** @brief 插件信息结构体 */
typedef struct {
  char name[PLUGIN_MAX_NAME_LEN];       /**< 插件名称 */
  char version[PLUGIN_MAX_VERSION_LEN]; /**< 插件版本 */
  char description[128];                /**< 插件描述 */
  char author[64];                      /**< 作者 */
  int api_version;                      /**< API版本 */
} plugin_info_t;

/** @brief 插件操作接口 */
typedef struct {
  /**
   * @brief 插件初始化
   * @param config 配置字符串（JSON格式）
   * @return 0成功, -1失败
   */
  int (*init)(const char *config);

  /**
   * @brief 插件启动
   * @return 0成功, -1失败
   */
  int (*start)(void);

  /**
   * @brief 插件停止
   * @return 0成功, -1失败
   */
  int (*stop)(void);

  /**
   * @brief 插件清理
   */
  void (*cleanup)(void);

  /**
   * @brief 获取插件信息
   * @return 插件信息结构体指针
   */
  const plugin_info_t *(*get_info)(void);

  /**
   * @brief 处理请求
   * @param request 请求数据
   * @param response 响应缓冲区
   * @param response_size 响应缓冲区大小
   * @return 0成功, -1失败
   */
  int (*handle_request)(const char *request, char *response, int response_size);
} plugin_ops_t;

/** @brief 插件上下文 */
typedef struct {
  char name[PLUGIN_MAX_NAME_LEN];       /**< 插件名称 */
  char path[PLUGIN_MAX_PATH_LEN];       /**< 插件路径 */
  plugin_state_t state;                 /**< 插件状态 */
  void *handle;                         /**< dlopen句柄 */
  const plugin_ops_t *ops;              /**< 插件操作接口 */
  plugin_info_t info;                   /**< 插件信息 */
  void *user_data;                      /**< 用户数据 */
} plugin_context_t;

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化插件管理器
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_manager_init(void);

/**
 * @brief 清理插件管理器资源
 */
void plugin_manager_cleanup(void);

/**
 * @brief 加载插件
 * @param path 插件文件路径
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_load(const char *path);

/**
 * @brief 卸载插件
 * @param name 插件名称
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_unload(const char *name);

/**
 * @brief 初始化插件
 * @param name 插件名称
 * @param config 配置字符串
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_init(const char *name, const char *config);

/**
 * @brief 启动插件
 * @param name 插件名称
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_start(const char *name);

/**
 * @brief 停止插件
 * @param name 插件名称
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_stop(const char *name);

/**
 * @brief 获取插件上下文
 * @param name 插件名称
 * @return 插件上下文指针, NULL未找到
 */
const plugin_context_t *plugin_get(const char *name);

/**
 * @brief 获取所有插件列表
 * @param plugins 输出插件名称数组
 * @param max_count 数组最大容量
 * @param count 实际插件数量
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_list(char plugins[][PLUGIN_MAX_NAME_LEN], int max_count,
                           int *count);

/**
 * @brief 加载目录中的所有插件
 * @param dir 插件目录路径
 * @return 成功加载的插件数量
 */
int plugin_load_all(const char *dir);

/**
 * @brief 处理插件请求
 * @param name 插件名称
 * @param request 请求数据
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return PLUGIN_OK成功
 */
plugin_error_t plugin_handle_request(const char *name, const char *request,
                                     char *response, int response_size);

/**
 * @brief 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *plugin_get_error_string(plugin_error_t error);

/**
 * @brief 打印插件状态
 */
void plugin_print_status(void);

/* ========================================================================== */
/*                              便捷宏定义 */
/* ========================================================================== */

/**
 * @brief 定义插件入口点
 *
 * 使用方法：
 * PLUGIN_DEFINE(my_plugin, "1.0.0", "My Plugin Description", "Author")
 * {
 *   // 插件实现...
 * }
 */
#define PLUGIN_DEFINE(name, ver, desc, auth)                                   \
  static int name##_init(const char *config);                                  \
  static int name##_start(void);                                               \
  static int name##_stop(void);                                                \
  static void name##_cleanup(void);                                            \
  static const plugin_info_t *name##_get_info(void);                           \
  static int name##_handle_request(const char *req, char *resp, int resp_size); \
                                                                               \
  static plugin_info_t name##_info = {                                         \
      .name = #name,                                                           \
      .version = ver,                                                          \
      .description = desc,                                                     \
      .author = auth,                                                          \
      .api_version = 1,                                                        \
  };                                                                           \
                                                                               \
  static const plugin_ops_t name##_ops = {                                     \
      .init = name##_init,                                                     \
      .start = name##_start,                                                   \
      .stop = name##_stop,                                                     \
      .cleanup = name##_cleanup,                                               \
      .get_info = name##_get_info,                                             \
      .handle_request = name##_handle_request,                                 \
  };                                                                           \
                                                                               \
  const plugin_ops_t *plugin_get_ops(void) { return &name##_ops; }             \
                                                                               \
  static const plugin_info_t *name##_get_info(void) { return &name##_info; }   \
                                                                               \
  static int name##_init(const char *config)

#ifdef __cplusplus
}
#endif

#endif /* PLUGIN_MANAGER_H */
