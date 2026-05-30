/**
 * @file web_api.c
 * @brief Web API处理函数实现
 * @author zhuxiangbo
 * @date 2026-05-24
 * @version 1.0
 *
 * 实现Web管理界面的REST API端点。
 */

#include "hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*                              API处理函数 */
/* ========================================================================== */

/**
 * @brief 获取所有传感器数据
 * @param method HTTP方法
 * @param path 请求路径
 * @param body 请求体
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功
 */
int api_get_sensors(const char *method, const char *path, const char *body,
                    char *response, int response_size) {
  (void)method;
  (void)path;
  (void)body;

  int pir = -1, light = -1, relay1 = -1, relay2 = -1, smoke_digital = -1;
  int humidity, temperature;

  /* 读取传感器数据 */
  hal_sensor_pir_read(&pir);
  hal_sensor_light_read(&light);
  hal_sensor_smoke_digital_read(&smoke_digital);
  hal_sensor_dht11_read(&humidity, &temperature);
  hal_relay1_read(&relay1);
  hal_relay2_read(&relay2);

  /* 构建JSON响应 */
  snprintf(response, response_size,
           "{\"pir\":%d,\"light\":%d,\"smoke_digital\":%d,"
           "\"temp\":%d,\"humi\":%d,"
           "\"relay\":%d,\"relay2\":%d}",
           pir, light, smoke_digital, temperature, humidity, relay1, relay2);

  return 0;
}

/**
 * @brief 获取继电器状态
 * @param method HTTP方法
 * @param path 请求路径（/api/relay/1 或 /api/relay/2）
 * @param body 请求体
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功, -1失败
 */
int api_get_relay(const char *method, const char *path, const char *body,
                  char *response, int response_size) {
  (void)method;
  (void)body;

  int relay_id = 0;
  int state = 0;
  hal_error_t ret;

  /* 解析继电器ID */
  if (strstr(path, "/relay/1")) {
    relay_id = 1;
  } else if (strstr(path, "/relay/2")) {
    relay_id = 2;
  } else {
    snprintf(response, response_size, "{\"error\":\"Invalid relay ID\"}");
    return -1;
  }

  /* 读取继电器状态 */
  if (relay_id == 1) {
    ret = hal_relay1_read(&state);
  } else {
    ret = hal_relay2_read(&state);
  }

  if (ret != HAL_OK) {
    snprintf(response, response_size, "{\"error\":\"Failed to read relay\"}");
    return -1;
  }

  snprintf(response, response_size, "{\"relay\":%d,\"state\":%d}", relay_id,
           state);
  return 0;
}

/**
 * @brief 控制继电器
 * @param method HTTP方法
 * @param path 请求路径（/api/relay/1/control 或 /api/relay/2/control）
 * @param body 请求体（JSON格式 {"state": 0/1}）
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功, -1失败
 */
int api_control_relay(const char *method, const char *path, const char *body,
                      char *response, int response_size) {
  (void)method;

  int relay_id = 0;
  int state = 0;
  hal_error_t ret;

  /* 解析继电器ID */
  if (strstr(path, "/relay/1/control")) {
    relay_id = 1;
  } else if (strstr(path, "/relay/2/control")) {
    relay_id = 2;
  } else {
    snprintf(response, response_size,
             "{\"success\":0,\"error\":\"Invalid relay ID\"}");
    return -1;
  }

  /* 解析请求体 */
  if (body) {
    const char *state_str = strstr(body, "\"state\"");
    if (state_str) {
      state_str = strchr(state_str, ':');
      if (state_str) {
        state = atoi(state_str + 1);
      }
    }
  }

  /* 控制继电器 */
  if (relay_id == 1) {
    ret = hal_relay1_control(state);
  } else {
    ret = hal_relay2_control(state);
  }

  if (ret != HAL_OK) {
    snprintf(response, response_size,
             "{\"success\":0,\"error\":\"Failed to control relay\"}");
    return -1;
  }

  snprintf(response, response_size, "{\"success\":1,\"relay\":%d,\"state\":%d}",
           relay_id, state);
  return 0;
}

/**
 * @brief 控制LED
 * @param method HTTP方法
 * @param path 请求路径
 * @param body 请求体（JSON格式 {"state": 0/1}）
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功, -1失败
 */
int api_control_led(const char *method, const char *path, const char *body,
                    char *response, int response_size) {
  (void)method;
  (void)path;

  int state = 0;
  hal_error_t ret;

  /* 解析请求体 */
  if (body) {
    const char *state_str = strstr(body, "\"state\"");
    if (state_str) {
      state_str = strchr(state_str, ':');
      if (state_str) {
        state = atoi(state_str + 1);
      }
    }
  }

  /* 控制LED */
  ret = hal_led_control(state);

  if (ret != HAL_OK) {
    snprintf(response, response_size,
             "{\"success\":0,\"error\":\"Failed to control LED\"}");
    return -1;
  }

  snprintf(response, response_size, "{\"success\":1,\"state\":%d}", state);
  return 0;
}

/**
 * @brief 获取系统状态
 * @param method HTTP方法
 * @param path 请求路径
 * @param body 请求体
 * @param response 响应缓冲区
 * @param response_size 响应缓冲区大小
 * @return 0成功
 */
int api_get_system_status(const char *method, const char *path,
                          const char *body, char *response,
                          int response_size) {
  (void)method;
  (void)path;
  (void)body;

  /* 获取系统运行时间 */
  FILE *uptime_file = fopen("/proc/uptime", "r");
  double uptime = 0;
  if (uptime_file) {
    fscanf(uptime_file, "%lf", &uptime);
    fclose(uptime_file);
  }

  /* 获取内存信息 */
  FILE *meminfo = fopen("/proc/meminfo", "r");
  long total_mem = 0;
  long free_mem = 0;
  if (meminfo) {
    char line[256];
    while (fgets(line, sizeof(line), meminfo)) {
      if (sscanf(line, "MemTotal: %ld kB", &total_mem) == 1)
        continue;
      if (sscanf(line, "MemFree: %ld kB", &free_mem) == 1)
        continue;
    }
    fclose(meminfo);
  }

  snprintf(response, response_size,
           "{\"uptime\":%.0f,\"mem_total\":%ld,\"mem_free\":%ld}", uptime,
           total_mem, free_mem);
  return 0;
}
