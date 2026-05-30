/**
 * @file rpc_client.cpp
 * @brief RPC客户端库 - 本地硬件通信接口
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 2.0
 *
 * 本模块实现了与本地RPC服务器的通信客户端，提供以下功能：
 * - LED控制
 * - DHT11温湿度读取
 * - PIR人体红外读取
 * - 光敏传感器读取
 * - 烟雾传感器读取
 * - 继电器控制与状态读取
 *
 * 特性：
 * - 自动重连机制
 * - 读取超时保护
 * - 线程安全（互斥锁保护）
 */

#include "cJSON.h"
#include "rpc.h"
#include "rpc_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief 读取超时时间（毫秒） */
#define READ_TIMEOUT_MS 3000

/** @brief 最大重试次数 */
#define MAX_RETRIES 10

/** @brief RPC服务器地址 */
#define RPC_SERVER_HOST "127.0.0.1"

/* ========================================================================== */
/*                              全局变量 */
/* ========================================================================== */

/** @brief RPC客户端socket描述符 */
static int g_iSocketClient = -1;

/** @brief 互斥锁 - 保护socket操作 */
static pthread_mutex_t rpc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                              函数声明 */
/* ========================================================================== */

int RPC_Client_Init(void);

/* ========================================================================== */
/*                              内部辅助函数 */
/* ========================================================================== */

/**
 * @brief 带超时的读取函数
 * @param sock socket描述符
 * @param buf 缓冲区
 * @param buf_size 缓冲区大小
 * @param timeout_ms 超时时间（毫秒）
 * @return 读取的字节数, 0表示超时, -1表示错误
 */
static int read_with_timeout(int sock, char *buf, int buf_size,
                             int timeout_ms) {
  fd_set read_fds;
  struct timeval tv;
  int ret;

  FD_ZERO(&read_fds);
  FD_SET(sock, &read_fds);

  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  ret = select(sock + 1, &read_fds, NULL, NULL, &tv);
  if (ret < 0) {
    printf("select error: %s\n", strerror(errno));
    return -1;
  }
  if (ret == 0) {
    printf("read timeout after %d ms\n", timeout_ms);
    return 0;
  }

  return read(sock, buf, buf_size - 1);
}

/**
 * @brief 安全发送函数（带重连）
 * @param buf 发送缓冲区
 * @param len 发送长度
 * @return 实际发送的字节数, -1表示错误
 *
 * 如果连接断开，会自动尝试重连。
 */
static int safe_send_locked(const char *buf, int len) {
  int ret;
  int sock = g_iSocketClient;

  /* 检查连接状态 */
  if (sock <= 0) {
    if (RPC_Client_Init() < 0) {
      return -1;
    }
    sock = g_iSocketClient;
  }

  /* 发送数据 */
  ret = send(sock, buf, len, 0);

  /* 检查连接是否断开 */
  if (ret <= 0 && (errno == EPIPE || errno == ECONNRESET)) {
    printf("Connection broken, reconnecting...\n");
    if (RPC_Client_Init() < 0) {
      return -1;
    }
    sock = g_iSocketClient;
    ret = send(sock, buf, len, 0);
  }

  return ret;
}

/**
 * @brief 读取响应并解析整数结果
 * @param result 输出参数，存储解析结果
 * @return 0成功, -1失败
 */
static int read_response_and_parse_locked(int *result) {
  char buf[300];
  int iLen;
  int sock = g_iSocketClient;
  int retry_count = 0;

  /* 读取响应（带重试） */
  do {
    iLen = read_with_timeout(sock, buf, sizeof(buf), READ_TIMEOUT_MS);
    if (iLen < 0) {
      printf("read rpc reply err: %d\n", iLen);
      return -1;
    }
    if (iLen == 0) {
      retry_count++;
      if (retry_count >= MAX_RETRIES) {
        printf("read timeout after %d retries\n", MAX_RETRIES);
        return -1;
      }
      continue;
    }
    buf[iLen] = 0;
  } while (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'));

  /* 解析JSON */
  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    printf("JSON parse error\n");
    return -1;
  }

  /* 提取result字段 */
  cJSON *result_obj = cJSON_GetObjectItem(root, "result");
  if (result_obj && cJSON_IsNumber(result_obj)) {
    *result = result_obj->valueint;
    cJSON_Delete(root);
    return 0;
  }

  cJSON_Delete(root);
  return -1;
}

/**
 * @brief 读取响应并解析数组结果（用于DHT11）
 * @param humi 输出参数，存储湿度值
 * @param temp 输出参数，存储温度值
 * @return 0成功, -1失败
 */
static int read_response_and_parse_array_locked(int *humi, int *temp) {
  char buf[300];
  int iLen;
  int sock = g_iSocketClient;
  int retry_count = 0;

  /* 读取响应（带重试） */
  do {
    iLen = read_with_timeout(sock, buf, sizeof(buf), READ_TIMEOUT_MS);
    if (iLen < 0) {
      printf("read rpc reply err: %d\n", iLen);
      return -1;
    }
    if (iLen == 0) {
      retry_count++;
      if (retry_count >= MAX_RETRIES) {
        printf("read timeout after %d retries\n", MAX_RETRIES);
        return -1;
      }
      continue;
    }
    buf[iLen] = 0;
  } while (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'));

  /* 解析JSON */
  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    return -1;
  }

  /* 提取result数组 */
  cJSON *result = cJSON_GetObjectItem(root, "result");
  if (result && cJSON_IsArray(result)) {
    cJSON *a = cJSON_GetArrayItem(result, 0);
    cJSON *b = cJSON_GetArrayItem(result, 1);
    if (a && b) {
      *humi = a->valueint;
      *temp = b->valueint;
      cJSON_Delete(root);
      return 0;
    }
  }

  cJSON_Delete(root);
  return -1;
}

/* ========================================================================== */
/*                              通用RPC调用函数 */
/* ========================================================================== */

/**
 * @brief 通用RPC调用（返回整数结果）
 * @param method 方法名
 * @param params 参数字符串（JSON格式）
 * @param result 输出参数，存储结果
 * @return 0成功, -1失败
 */
static int rpc_call_int_result(const char *method, const char *params,
                               int *result) {
  char buf[200];
  int ret;
  int local_result;

  /* 构建请求 */
  sprintf(buf, "{\"method\": \"%s\", \"params\": [%s], \"id\": \"1\" }", method,
          params);

  pthread_mutex_lock(&rpc_mutex);

  /* 发送请求 */
  ret = safe_send_locked(buf, strlen(buf));
  if (ret != (int)strlen(buf)) {
    printf("send rpc request err: %d, %s\n", ret, strerror(errno));
    pthread_mutex_unlock(&rpc_mutex);
    return -1;
  }

  /* 读取响应 */
  ret = read_response_and_parse_locked(&local_result);
  pthread_mutex_unlock(&rpc_mutex);

  if (ret == 0) {
    *result = local_result;
    return 0;
  }

  return -1;
}

/**
 * @brief 通用RPC调用（无返回值）
 * @param method 方法名
 * @param params 参数字符串（JSON格式）
 * @return 0成功, -1失败
 */
static int rpc_call_no_result(const char *method, const char *params) {
  int result;
  return rpc_call_int_result(method, params, &result);
}

/* ========================================================================== */
/*                              RPC接口函数 */
/* ========================================================================== */

/**
 * @brief LED控制
 * @param on 0关闭, 1打开
 * @return 0成功, -1失败
 */
int rpc_led_control(int on) {
  char params[16];
  sprintf(params, "%d", on);
  return rpc_call_no_result("led_control", params);
}

/**
 * @brief DHT11温湿度读取
 * @param humi 输出参数，湿度值
 * @param temp 输出参数，温度值
 * @return 0成功, -1失败
 */
int rpc_dht11_read(char *humi, char *temp) {
  char buf[200];
  int ret;
  int h, t;

  sprintf(buf, "{\"method\": \"dht11_read\", \"params\": [0], \"id\": \"2\" }");

  pthread_mutex_lock(&rpc_mutex);

  ret = safe_send_locked(buf, strlen(buf));
  if (ret != (int)strlen(buf)) {
    printf("send rpc request err: %d, %s\n", ret, strerror(errno));
    pthread_mutex_unlock(&rpc_mutex);
    return -1;
  }

  ret = read_response_and_parse_array_locked(&h, &t);
  pthread_mutex_unlock(&rpc_mutex);

  if (ret == 0) {
    *humi = (char)h;
    *temp = (char)t;
    return 0;
  }

  return -1;
}

/**
 * @brief PIR人体红外读取
 * @param value 输出参数，0无人, 1有人
 * @return 0成功, -1失败
 */
int rpc_pir_read(int *value) {
  return rpc_call_int_result("pir_read", "", value);
}

/**
 * @brief 光敏传感器读取
 * @param value 输出参数，0明亮, 1黑暗
 * @return 0成功, -1失败
 */
int rpc_light_read(int *value) {
  return rpc_call_int_result("light_read", "", value);
}

/**
 * @brief 继电器1控制（风扇）
 * @param on 0关闭, 1打开
 * @return 0成功, -1失败
 */
int rpc_relay_control(int on) {
  char params[16];
  sprintf(params, "%d", on);
  return rpc_call_no_result("relay_control", params);
}

/**
 * @brief 继电器1状态读取
 * @param value 输出参数，0关闭, 1打开
 * @return 0成功, -1失败
 */
int rpc_relay_read(int *value) {
  return rpc_call_int_result("relay_read", "", value);
}

/**
 * @brief 烟雾传感器数字读取
 * @param value 输出参数，0检测到烟雾, 1正常
 * @return 0成功, -1失败
 */
int rpc_smoke_digital_read(int *value) {
  return rpc_call_int_result("smoke_digital_read", "", value);
}

/**
 * @brief 继电器2控制（LED灯）
 * @param on 0关闭, 1打开
 * @return 0成功, -1失败
 */
int rpc_relay2_control(int on) {
  char params[16];
  sprintf(params, "%d", on);
  return rpc_call_no_result("relay2_control", params);
}

/**
 * @brief 继电器2状态读取
 * @param value 输出参数，0关闭, 1打开
 * @return 0成功, -1失败
 */
int rpc_relay2_read(int *value) {
  return rpc_call_int_result("relay2_read", "", value);
}

/* ========================================================================== */
/*                              连接管理 */
/* ========================================================================== */

/**
 * @brief 初始化RPC客户端连接
 * @return socket描述符, -1表示失败
 *
 * 如果已有连接，会先关闭再重新连接。
 */
int RPC_Client_Init(void) {
  int iSocketClient;
  struct sockaddr_in tSocketServerAddr;
  int iRet;

  /* 关闭旧连接 */
  if (g_iSocketClient > 0) {
    close(g_iSocketClient);
    g_iSocketClient = -1;
  }

  /* 创建socket */
  iSocketClient = socket(AF_INET, SOCK_STREAM, 0);
  if (iSocketClient < 0) {
    printf("socket error\n");
    return -1;
  }

  /* 配置服务器地址 */
  tSocketServerAddr.sin_family = AF_INET;
  tSocketServerAddr.sin_port = htons(PORT);
  inet_aton(RPC_SERVER_HOST, &tSocketServerAddr.sin_addr);
  memset(tSocketServerAddr.sin_zero, 0, 8);

  /* 连接服务器 */
  iRet = connect(iSocketClient, (const struct sockaddr *)&tSocketServerAddr,
                 sizeof(struct sockaddr));
  if (-1 == iRet) {
    printf("connect error!\n");
    close(iSocketClient);
    return -1;
  }

  g_iSocketClient = iSocketClient;
  printf("RPC_Client_Init connected successfully\n");
  return iSocketClient;
}
