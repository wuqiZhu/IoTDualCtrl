/**
 * @file rpc_server.c
 * @brief RPC服务器 - 硬件抽象层接口
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 2.0
 *
 * 本模块实现了基于JSON-RPC的硬件控制服务器，提供以下功能：
 * - LED灯控制
 * - DHT11温湿度传感器读取
 * - PIR人体红外传感器读取
 * - 光敏传感器读取
 * - 烟雾传感器读取（数字信号）
 * - 继电器控制（风扇、LED灯）
 *
 * 服务器监听端口1234，支持多个客户端同时连接。
 */

#include "dht11.h"
#include "hal.h"
#include "http_server.h"
#include "led.h"
#include "rpc.h"
#include "watchdog.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <jsonrpc-c.h>
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
/*                              常量定义 */
/* ========================================================================== */

/** @brief DHT11最大重试次数 */
#define DHT11_MAX_RETRIES 10

/** @brief DHT11重试间隔（微秒） */
#define DHT11_RETRY_DELAY_US 1000

/* ========================================================================== */
/*                              辅助宏定义 */
/* ========================================================================== */

/**
 * @brief 验证RPC参数是否有效
 * @param params 参数对象
 * @return 如果参数无效返回错误响应，否则继续执行
 */
#define VALIDATE_PARAMS(params)                                                \
  do {                                                                         \
    if (!(params) || !cJSON_IsArray(params) ||                                 \
        cJSON_GetArraySize(params) < 1) {                                      \
      return cJSON_CreateNumber(-1);                                           \
    }                                                                          \
  } while (0)

/**
 * @brief 验证RPC参数并获取第一个数字参数
 * @param params 参数对象
 * @param out_var 输出变量
 * @return 如果参数无效返回错误响应，否则将参数值存入out_var
 */
#define VALIDATE_AND_GET_PARAM(params, out_var)                                \
  do {                                                                         \
    VALIDATE_PARAMS(params);                                                   \
    cJSON *_param = cJSON_GetArrayItem(params, 0);                             \
    if (!_param || !cJSON_IsNumber(_param)) {                                  \
      return cJSON_CreateNumber(-1);                                           \
    }                                                                          \
    (out_var) = _param->valueint;                                              \
  } while (0)

/* ========================================================================== */
/*                              全局变量 */
/* ========================================================================== */

/** @brief RPC服务器实例 */
static struct jrpc_server my_server;

/** @brief 看门狗喂狗线程运行标志 */
static volatile int watchdog_feed_running = 0;

/** @brief 看门狗喂狗线程 */
static pthread_t watchdog_feed_thread;

/* ========================================================================== */
/*                              RPC方法实现 */
/* ========================================================================== */

/**
 * @brief LED控制RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组 [status]
 * @param id 请求ID
 * @return cJSON响应对象
 *
 * 参数说明：
 *   - status: 0关闭, 1打开
 */
cJSON *server_led_control(jrpc_context *ctx, cJSON *params, cJSON *id) {
  (void)ctx;
  (void)id;
  int status;
  VALIDATE_AND_GET_PARAM(params, status);
  led_control(status);
  return cJSON_CreateNumber(0);
}

/**
 * @brief DHT11温湿度读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数组 [humidity, temperature]
 *
 * 返回值说明：
 *   - humidity: 湿度百分比
 *   - temperature: 温度摄氏度
 *   - 失败时返回 [0, 0]
 */
cJSON *server_dht11_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int array[2];
  array[0] = array[1] = 0;
  int retry_count = 0;

  while (0 != dht11_read((char *)&array[0], (char *)&array[1])) {
    retry_count++;
    if (retry_count >= DHT11_MAX_RETRIES) {
      printf("DHT11 read failed after %d retries\n", DHT11_MAX_RETRIES);
      return cJSON_CreateIntArray(array, 2);
    }
    usleep(DHT11_RETRY_DELAY_US);
  }

  return cJSON_CreateIntArray(array, 2);
}

/**
 * @brief PIR人体红外读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数字 (0=无人, 1=有人, -1=错误)
 */
cJSON *server_pir_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int value;
  hal_error_t ret = hal_sensor_pir_read(&value);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(value);
}

/**
 * @brief 光敏传感器读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数字 (0=明亮, 1=黑暗)
 *
 * 阈值：ADC值 < 2000 为黑暗
 */
cJSON *server_light_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int value;
  hal_error_t ret = hal_sensor_light_read(&value);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(value);
}

/**
 * @brief 继电器1控制RPC方法（风扇）
 * @param ctx RPC上下文
 * @param params 参数数组 [status]
 * @param id 请求ID
 * @return cJSON数字 (0=成功, -1=失败)
 *
 * 参数说明：
 *   - status: 0关闭, 1打开
 */
cJSON *server_relay_control(jrpc_context *ctx, cJSON *params, cJSON *id) {
  (void)ctx;
  (void)id;
  int new_state;
  VALIDATE_AND_GET_PARAM(params, new_state);
  hal_error_t ret = hal_relay1_control(new_state);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(0);
}

/**
 * @brief 继电器1状态读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数字 (0=关闭, 1=打开)
 */
cJSON *server_relay_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int state;
  hal_error_t ret = hal_relay1_read(&state);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(state);
}

/**
 * @brief 烟雾传感器数字读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数字 (0=检测到烟雾, 1=正常, -1=错误)
 */
cJSON *server_smoke_digital_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int value;
  hal_error_t ret = hal_sensor_smoke_digital_read(&value);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(value);
}

/**
 * @brief 继电器2控制RPC方法（LED灯）
 * @param ctx RPC上下文
 * @param params 参数数组 [status]
 * @param id 请求ID
 * @return cJSON数字 (0=成功, -1=失败)
 *
 * 参数说明：
 *   - status: 0关闭, 1打开
 */
cJSON *server_relay2_control(jrpc_context *ctx, cJSON *params, cJSON *id) {
  (void)ctx;
  (void)id;
  int new_state;
  VALIDATE_AND_GET_PARAM(params, new_state);
  hal_error_t ret = hal_relay2_control(new_state);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(0);
}

/**
 * @brief 继电器2状态读取RPC方法
 * @param ctx RPC上下文
 * @param params 参数数组（未使用）
 * @param id 请求ID
 * @return cJSON数字 (0=关闭, 1=打开)
 */
cJSON *server_relay2_read(jrpc_context *ctx, cJSON *params, cJSON *id) {
  int state;
  hal_error_t ret = hal_relay2_read(&state);
  if (ret != HAL_OK) {
    return cJSON_CreateNumber(-1);
  }
  return cJSON_CreateNumber(state);
}

/* ========================================================================== */
/*                              RPC服务器管理 */
/* ========================================================================== */

/**
 * @brief 初始化并启动RPC服务器
 * @return 0成功
 *
 * 注册所有RPC方法并启动服务器事件循环。
 * 服务器将阻塞在此函数中，直到收到退出信号。
 */
int RPC_Server_Init(void) {
  int err;

  err = jrpc_server_init(&my_server, PORT);
  if (err) {
    printf("jrpc_server_init err: %d\n", err);
    return -1;
  }

  /* 注册硬件控制方法 */
  jrpc_register_procedure(&my_server, server_led_control, "led_control", NULL);
  jrpc_register_procedure(&my_server, server_dht11_read, "dht11_read", NULL);
  jrpc_register_procedure(&my_server, server_pir_read, "pir_read", NULL);
  jrpc_register_procedure(&my_server, server_light_read, "light_read", NULL);
  jrpc_register_procedure(&my_server, server_relay_control, "relay_control",
                          NULL);
  jrpc_register_procedure(&my_server, server_relay_read, "relay_read", NULL);
  jrpc_register_procedure(&my_server, server_smoke_digital_read,
                          "smoke_digital_read", NULL);
  jrpc_register_procedure(&my_server, server_relay2_control, "relay2_control",
                          NULL);
  jrpc_register_procedure(&my_server, server_relay2_read, "relay2_read", NULL);

  printf("RPC server started, listening on port %d\n", PORT);

  /* 运行服务器（阻塞） */
  jrpc_server_run(&my_server);
  jrpc_server_destroy(&my_server);

  return 0;
}

/* ========================================================================== */
/*                              看门狗喂狗线程 */
/* ========================================================================== */

/**
 * @brief 看门狗喂狗线程函数
 * @param arg 未使用
 * @return NULL
 *
 * 定期调用 watchdog_feed() 重置看门狗计时器。
 * 如果主事件循环卡死，此线程也将无法运行，看门狗将触发超时。
 */
static void *watchdog_feed_thread_func(void *arg) {
  (void)arg;
  printf("Watchdog feed thread started\n");

  while (watchdog_feed_running) {
    watchdog_feed();
    sleep(WATCHDOG_DEFAULT_FEED_INTERVAL_SEC);
  }

  printf("Watchdog feed thread exiting\n");
  return NULL;
}

/* ========================================================================== */
/*                              资源清理 */
/* ========================================================================== */

/**
 * @brief 清理所有硬件资源
 *
 * 关闭所有继电器，取消导出所有GPIO引脚。
 * 通过atexit()注册，确保程序退出时自动调用。
 */
static void cleanup_resources(void) {
  printf("Cleaning up resources...\n");

  /* 停止HTTP服务器 */
  http_server_cleanup();

  /* 停止看门狗喂狗线程 */
  if (watchdog_feed_running) {
    watchdog_feed_running = 0;
    pthread_join(watchdog_feed_thread, NULL);
  }

  /* 停止并清理看门狗 */
  watchdog_cleanup();

  /* 清理HAL层 */
  hal_cleanup();
  printf("Resources cleaned up\n");
}

/**
 * @brief 信号处理函数
 * @param sig 信号编号
 *
 * 处理SIGINT和SIGTERM信号，实现优雅退出。
 */
static void signal_handler(int sig) {
  printf("\nReceived signal %d, exiting...\n", sig);
  cleanup_resources();
  exit(0);
}

/* ========================================================================== */
/*                              主函数 */
/* ========================================================================== */

/**
 * @brief 程序入口
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 0成功, -1失败
 *
 * 初始化流程：
 * 1. 初始化LED和DHT11驱动
 * 2. 注册信号处理和清理函数
 * 3. 导出并配置GPIO引脚
 * 4. 启动RPC服务器
 */
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  /* 初始化HAL层（使用默认配置） */
  hal_error_t ret = hal_init(NULL);
  if (ret != HAL_OK) {
    printf("HAL initialization failed: %s\n", hal_get_error_string(ret));
    return -1;
  }

  /* 初始化看门狗（超时30秒） */
  watchdog_error_t wd_ret = watchdog_init(30, NULL, NULL);
  if (wd_ret != WATCHDOG_OK) {
    printf("Watchdog initialization failed: %s\n",
           watchdog_get_error_string(wd_ret));
    hal_cleanup();
    return -1;
  }

  /* 启动看门狗 */
  wd_ret = watchdog_start();
  if (wd_ret != WATCHDOG_OK) {
    printf("Watchdog start failed: %s\n", watchdog_get_error_string(wd_ret));
    watchdog_cleanup();
    hal_cleanup();
    return -1;
  }

  /* 启动看门狗喂狗线程 */
  watchdog_feed_running = 1;
  if (pthread_create(&watchdog_feed_thread, NULL, watchdog_feed_thread_func,
                     NULL) != 0) {
    printf("Failed to create watchdog feed thread\n");
    watchdog_cleanup();
    hal_cleanup();
    return -1;
  }

  /* 注册信号处理 */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* 注册退出清理 */
  atexit(cleanup_resources);

  /* 初始化HTTP服务器 */
  http_error_t http_ret = http_server_init(8080, "www");
  if (http_ret != HTTP_OK) {
    printf("HTTP server initialization failed: %s\n",
           http_server_get_error_string(http_ret));
    /* HTTP服务器初始化失败不影响RPC服务器运行 */
  } else {
    /* 注册API处理函数 */
    extern int api_get_sensors(const char *, const char *, const char *, char *,
                               int);
    extern int api_get_relay(const char *, const char *, const char *, char *,
                             int);
    extern int api_control_relay(const char *, const char *, const char *,
                                 char *, int);
    extern int api_control_led(const char *, const char *, const char *, char *,
                               int);
    extern int api_get_system_status(const char *, const char *, const char *,
                                     char *, int);

    http_server_register_api("/api/sensors", api_get_sensors);
    http_server_register_api("/api/relay/1", api_get_relay);
    http_server_register_api("/api/relay/2", api_get_relay);
    http_server_register_api("/api/relay/1/control", api_control_relay);
    http_server_register_api("/api/relay/2/control", api_control_relay);
    http_server_register_api("/api/led/control", api_control_led);
    http_server_register_api("/api/system", api_get_system_status);

    /* 启动HTTP服务器 */
    http_ret = http_server_start();
    if (http_ret != HTTP_OK) {
      printf("HTTP server start failed: %s\n",
             http_server_get_error_string(http_ret));
    } else {
      printf("Web management interface available at http://<device_ip>:8080\n");
    }
  }

  printf("RPC Server starting on port %d...\n", PORT);
  RPC_Server_Init();

  return 0;
}
