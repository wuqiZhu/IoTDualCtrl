/**
 * @file http_server.h
 * @brief 轻量级HTTP服务器接口定义
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 提供简单的HTTP服务器功能，用于Web管理界面。
 * 支持静态文件服务和REST API端点。
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              配置参数 */
/* ========================================================================== */

/** @brief HTTP服务器默认端口 */
#define HTTP_DEFAULT_PORT 8080

/** @brief HTTP请求缓冲区大小 */
#define HTTP_BUFFER_SIZE 4096

/** @brief HTTP响应最大大小 */
#define HTTP_RESPONSE_MAX_SIZE 8192

/** @brief 静态文件根目录最大长度 */
#define HTTP_ROOT_MAX_LEN 256

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief HTTP服务器错误码 */
typedef enum {
  HTTP_OK = 0,               /**< 操作成功 */
  HTTP_ERROR = -1,           /**< 通用错误 */
  HTTP_ERROR_SOCKET = -2,    /**< Socket错误 */
  HTTP_ERROR_BIND = -3,      /**< 绑定错误 */
  HTTP_ERROR_LISTEN = -4,    /**< 监听错误 */
  HTTP_ERROR_ACCEPT = -5,    /**< 接受连接错误 */
  HTTP_ERROR_THREAD = -6,    /**< 线程错误 */
} http_error_t;

/* ========================================================================== */
/*                              回调函数类型 */
/* ========================================================================== */

/**
 * @brief API请求处理回调函数类型
 * @param method HTTP方法（GET, POST等）
 * @param path 请求路径
 * @param body 请求体（POST请求）
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功，-1失败
 *
 * 处理API请求并生成JSON响应。
 */
typedef int (*http_api_handler_t)(const char *method, const char *path,
                                  const char *body, char *response,
                                  int response_size);

/* ========================================================================== */
/*                              接口函数 */
/* ========================================================================== */

/**
 * @brief 初始化HTTP服务器
 * @param port 服务器端口，0使用默认端口8080
 * @param root_dir 静态文件根目录，NULL使用当前目录
 * @return HTTP_OK成功
 */
http_error_t http_server_init(int port, const char *root_dir);

/**
 * @brief 注册API处理函数
 * @param path API路径（如 "/api/sensors"）
 * @param handler 处理函数
 * @return HTTP_OK成功
 */
http_error_t http_server_register_api(const char *path,
                                      http_api_handler_t handler);

/**
 * @brief 启动HTTP服务器
 * @return HTTP_OK成功
 *
 * 启动HTTP服务器线程，开始监听HTTP请求。
 */
http_error_t http_server_start(void);

/**
 * @brief 停止HTTP服务器
 * @return HTTP_OK成功
 */
http_error_t http_server_stop(void);

/**
 * @brief 获取HTTP服务器错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *http_server_get_error_string(http_error_t error);

/**
 * @brief 清理HTTP服务器资源
 */
void http_server_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */
