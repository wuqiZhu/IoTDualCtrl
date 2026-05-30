/**
 * @file http_server.c
 * @brief 轻量级HTTP服务器实现
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 使用标准POSIX socket实现简单的HTTP服务器。
 * 支持静态文件服务和REST API端点。
 */

#include "http_server.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部数据结构 */
/* ========================================================================== */

/** @brief API处理函数节点 */
typedef struct api_node {
  char path[256];               /**< API路径 */
  http_api_handler_t handler;   /**< 处理函数 */
  struct api_node *next;        /**< 下一个节点 */
} api_node_t;

/** @brief HTTP服务器上下文 */
typedef struct {
  int initialized;              /**< 初始化标志 */
  int running;                  /**< 运行标志 */
  int server_fd;                /**< 服务器socket */
  int port;                     /**< 服务器端口 */
  char root_dir[HTTP_ROOT_MAX_LEN]; /**< 静态文件根目录 */
  pthread_t server_thread;      /**< 服务器线程 */
  api_node_t *api_list;         /**< API处理函数链表 */
} http_context_t;

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief HTTP服务器全局上下文 */
static http_context_t g_http = {
    .initialized = 0,
    .running = 0,
    .server_fd = -1,
    .port = HTTP_DEFAULT_PORT,
    .root_dir = ".",
    .server_thread = 0,
    .api_list = NULL,
};

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 获取文件的MIME类型
 * @param path 文件路径
 * @return MIME类型字符串
 */
static const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "application/octet-stream";

  if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
    return "text/html";
  if (strcmp(ext, ".css") == 0)
    return "text/css";
  if (strcmp(ext, ".js") == 0)
    return "application/javascript";
  if (strcmp(ext, ".json") == 0)
    return "application/json";
  if (strcmp(ext, ".png") == 0)
    return "image/png";
  if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
    return "image/jpeg";
  if (strcmp(ext, ".gif") == 0)
    return "image/gif";
  if (strcmp(ext, ".svg") == 0)
    return "image/svg+xml";
  if (strcmp(ext, ".ico") == 0)
    return "image/x-icon";
  if (strcmp(ext, ".txt") == 0)
    return "text/plain";

  return "application/octet-stream";
}

/**
 * @brief 发送HTTP响应
 * @param client_fd 客户端socket
 * @param status HTTP状态码
 * @param content_type 内容类型
 * @param body 响应体
 * @param body_len 响应体长度
 */
static void send_response(int client_fd, int status,
                          const char *content_type, const char *body,
                          int body_len) {
  char header[512];
  int header_len;

  const char *status_text;
  switch (status) {
  case 200:
    status_text = "OK";
    break;
  case 404:
    status_text = "Not Found";
    break;
  case 500:
    status_text = "Internal Server Error";
    break;
  default:
    status_text = "Unknown";
    break;
  }

  header_len = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "\r\n",
                        status, status_text, content_type, body_len);

  write(client_fd, header, header_len);
  if (body && body_len > 0) {
    write(client_fd, body, body_len);
  }
}

/**
 * @brief 发送错误响应
 * @param client_fd 客户端socket
 * @param status HTTP状态码
 * @param message 错误信息
 */
static void send_error(int client_fd, int status, const char *message) {
  char body[256];
  int body_len = snprintf(body, sizeof(body),
                          "{\"error\":\"%s\",\"status\":%d}", message, status);
  send_response(client_fd, status, "application/json", body, body_len);
}

/**
 * @brief 查找API处理函数
 * @param path API路径
 * @return 处理函数指针，未找到返回NULL
 */
static http_api_handler_t find_api_handler(const char *path) {
  api_node_t *node = g_http.api_list;
  while (node) {
    if (strcmp(node->path, path) == 0) {
      return node->handler;
    }
    node = node->next;
  }
  return NULL;
}

/**
 * @brief 处理静态文件请求
 * @param client_fd 客户端socket
 * @param path 请求路径
 */
static void handle_static_file(int client_fd, const char *path) {
  char filepath[512];
  struct stat st;

  /* 构建文件路径 */
  if (strcmp(path, "/") == 0) {
    snprintf(filepath, sizeof(filepath), "%s/index.html", g_http.root_dir);
  } else {
    snprintf(filepath, sizeof(filepath), "%s%s", g_http.root_dir, path);
  }

  /* 检查文件是否存在 */
  if (stat(filepath, &st) != 0 || S_ISDIR(st.st_mode)) {
    send_error(client_fd, 404, "File not found");
    return;
  }

  /* 读取文件内容 */
  FILE *fp = fopen(filepath, "rb");
  if (!fp) {
    send_error(client_fd, 500, "Failed to open file");
    return;
  }

  char *content = malloc(st.st_size + 1);
  if (!content) {
    fclose(fp);
    send_error(client_fd, 500, "Out of memory");
    return;
  }

  size_t read_size = fread(content, 1, st.st_size, fp);
  fclose(fp);

  /* 发送响应 */
  send_response(client_fd, 200, get_mime_type(filepath), content, read_size);
  free(content);
}

/**
 * @brief 处理HTTP请求
 * @param client_fd 客户端socket
 */
static void handle_request(int client_fd) {
  char buffer[HTTP_BUFFER_SIZE];
  int bytes_read;

  /* 读取请求 */
  bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
  if (bytes_read <= 0) {
    close(client_fd);
    return;
  }
  buffer[bytes_read] = '\0';

  /* 解析请求行 */
  char method[16] = {0};
  char path[256] = {0};
  char version[16] = {0};

  if (sscanf(buffer, "%15s %255s %15s", method, path, version) != 3) {
    send_error(client_fd, 400, "Bad request");
    close(client_fd);
    return;
  }

  printf("[HTTP] %s %s\n", method, path);

  /* 查找请求体 */
  char *body = strstr(buffer, "\r\n\r\n");
  if (body) {
    body += 4; /* 跳过 \r\n\r\n */
  }

  /* 检查是否是API请求 */
  if (strncmp(path, "/api/", 5) == 0) {
    http_api_handler_t handler = find_api_handler(path);
    if (handler) {
      char response[HTTP_RESPONSE_MAX_SIZE] = {0};
      int ret = handler(method, path, body, response, sizeof(response));
      if (ret == 0 && response[0] != '\0') {
        send_response(client_fd, 200, "application/json", response,
                      strlen(response));
      } else {
        send_error(client_fd, 500, "API handler failed");
      }
    } else {
      send_error(client_fd, 404, "API endpoint not found");
    }
  } else {
    /* 处理静态文件请求 */
    handle_static_file(client_fd, path);
  }

  close(client_fd);
}

/**
 * @brief HTTP服务器线程函数
 * @param arg 未使用
 * @return NULL
 */
static void *http_server_thread(void *arg) {
  (void)arg;

  printf("[HTTP] Server started on port %d\n", g_http.port);
  printf("[HTTP] Serving files from: %s\n", g_http.root_dir);

  while (g_http.running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(g_http.server_fd, (struct sockaddr *)&client_addr,
                           &client_len);
    if (client_fd < 0) {
      if (g_http.running) {
        perror("[HTTP] accept failed");
      }
      continue;
    }

    /* 处理请求（简化版本，单线程处理） */
    handle_request(client_fd);
  }

  printf("[HTTP] Server thread exiting\n");
  return NULL;
}

/* ========================================================================== */
/*                              接口实现 */
/* ========================================================================== */

http_error_t http_server_init(int port, const char *root_dir) {
  if (g_http.initialized) {
    return HTTP_ERROR;
  }

  /* 设置端口 */
  if (port > 0) {
    g_http.port = port;
  }

  /* 设置根目录 */
  if (root_dir) {
    strncpy(g_http.root_dir, root_dir, HTTP_ROOT_MAX_LEN - 1);
  } else {
    getcwd(g_http.root_dir, HTTP_ROOT_MAX_LEN);
  }

  /* 创建socket */
  g_http.server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (g_http.server_fd < 0) {
    perror("[HTTP] socket creation failed");
    return HTTP_ERROR_SOCKET;
  }

  /* 设置socket选项 */
  int opt = 1;
  if (setsockopt(g_http.server_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                 sizeof(opt)) < 0) {
    perror("[HTTP] setsockopt failed");
    close(g_http.server_fd);
    return HTTP_ERROR_SOCKET;
  }

  /* 绑定地址 */
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(g_http.port);

  if (bind(g_http.server_fd, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("[HTTP] bind failed");
    close(g_http.server_fd);
    return HTTP_ERROR_BIND;
  }

  /* 开始监听 */
  if (listen(g_http.server_fd, 5) < 0) {
    perror("[HTTP] listen failed");
    close(g_http.server_fd);
    return HTTP_ERROR_LISTEN;
  }

  g_http.initialized = 1;
  printf("[HTTP] Server initialized on port %d\n", g_http.port);
  return HTTP_OK;
}

http_error_t http_server_register_api(const char *path,
                                      http_api_handler_t handler) {
  if (!path || !handler) {
    return HTTP_ERROR;
  }

  api_node_t *node = malloc(sizeof(api_node_t));
  if (!node) {
    return HTTP_ERROR;
  }

  strncpy(node->path, path, sizeof(node->path) - 1);
  node->handler = handler;
  node->next = g_http.api_list;
  g_http.api_list = node;

  printf("[HTTP] Registered API: %s\n", path);
  return HTTP_OK;
}

http_error_t http_server_start(void) {
  if (!g_http.initialized) {
    return HTTP_ERROR;
  }

  if (g_http.running) {
    return HTTP_ERROR;
  }

  g_http.running = 1;

  /* 创建服务器线程 */
  if (pthread_create(&g_http.server_thread, NULL, http_server_thread, NULL) !=
      0) {
    perror("[HTTP] Failed to create server thread");
    g_http.running = 0;
    return HTTP_ERROR_THREAD;
  }

  /* 分离线程 */
  pthread_detach(g_http.server_thread);

  return HTTP_OK;
}

http_error_t http_server_stop(void) {
  if (!g_http.running) {
    return HTTP_ERROR;
  }

  g_http.running = 0;

  /* 关闭socket以中断accept */
  if (g_http.server_fd >= 0) {
    close(g_http.server_fd);
    g_http.server_fd = -1;
  }

  usleep(100000); /* 等待线程退出 */

  printf("[HTTP] Server stopped\n");
  return HTTP_OK;
}

const char *http_server_get_error_string(http_error_t error) {
  switch (error) {
  case HTTP_OK:
    return "Success";
  case HTTP_ERROR:
    return "Generic error";
  case HTTP_ERROR_SOCKET:
    return "Socket error";
  case HTTP_ERROR_BIND:
    return "Bind error";
  case HTTP_ERROR_LISTEN:
    return "Listen error";
  case HTTP_ERROR_ACCEPT:
    return "Accept error";
  case HTTP_ERROR_THREAD:
    return "Thread error";
  default:
    return "Unknown error";
  }
}

void http_server_cleanup(void) {
  /* 停止服务器 */
  http_server_stop();

  /* 释放API链表 */
  api_node_t *node = g_http.api_list;
  while (node) {
    api_node_t *next = node->next;
    free(node);
    node = next;
  }
  g_http.api_list = NULL;

  g_http.initialized = 0;
  printf("[HTTP] Server cleanup completed\n");
}
