/**
 * @file error.c
 * @brief 错误处理框架实现
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 */

#include "error.h"
#include <stdio.h>

/**
 * @brief 错误码与错误信息映射表
 */
typedef struct {
  error_code_t code;
  const char *message;
} error_info_t;

/** @brief 错误信息表 */
static const error_info_t error_table[] = {
    /* 成功 */
    {ERR_SUCCESS, "Success"},

    /* 通用错误 */
    {ERR_INVALID_PARAM, "Invalid parameter"},
    {ERR_NULL_POINTER, "Null pointer"},
    {ERR_OUT_OF_MEMORY, "Out of memory"},
    {ERR_TIMEOUT, "Operation timeout"},
    {ERR_NOT_FOUND, "Not found"},
    {ERR_ALREADY_EXISTS, "Already exists"},
    {ERR_NOT_INITIALIZED, "Not initialized"},
    {ERR_BUSY, "Resource busy"},
    {ERR_ABORTED, "Operation aborted"},

    /* GPIO错误 */
    {ERR_GPIO_EXPORT, "GPIO export failed"},
    {ERR_GPIO_DIRECTION, "GPIO direction set failed"},
    {ERR_GPIO_READ, "GPIO read failed"},
    {ERR_GPIO_WRITE, "GPIO write failed"},
    {ERR_GPIO_INVALID_PIN, "Invalid GPIO pin"},

    /* ADC错误 */
    {ERR_ADC_OPEN, "ADC device open failed"},
    {ERR_ADC_READ, "ADC read failed"},

    /* 传感器错误 */
    {ERR_SENSOR_DHT11, "DHT11 sensor read failed"},
    {ERR_SENSOR_PIR, "PIR sensor read failed"},
    {ERR_SENSOR_LIGHT, "Light sensor read failed"},
    {ERR_SENSOR_SMOKE, "Smoke sensor read failed"},

    /* 继电器错误 */
    {ERR_RELAY_CONTROL, "Relay control failed"},
    {ERR_RELAY_READ, "Relay status read failed"},

    /* 网络错误 */
    {ERR_SOCKET_CREATE, "Socket create failed"},
    {ERR_SOCKET_CONNECT, "Socket connect failed"},
    {ERR_SOCKET_SEND, "Socket send failed"},
    {ERR_SOCKET_RECV, "Socket receive failed"},
    {ERR_SOCKET_TIMEOUT, "Socket timeout"},

    /* RPC错误 */
    {ERR_RPC_INIT, "RPC initialization failed"},
    {ERR_RPC_CALL, "RPC call failed"},
    {ERR_RPC_PARSE, "RPC response parse failed"},

    /* MQTT错误 */
    {ERR_MQTT_INIT, "MQTT initialization failed"},
    {ERR_MQTT_CONNECT, "MQTT connection failed"},
    {ERR_MQTT_SUBSCRIBE, "MQTT subscribe failed"},
    {ERR_MQTT_PUBLISH, "MQTT publish failed"},
    {ERR_MQTT_DISCONNECT, "MQTT disconnection"},

    /* 配置错误 */
    {ERR_CONFIG_FILE, "Config file error"},
    {ERR_CONFIG_PARSE, "Config parse error"},
    {ERR_CONFIG_MISSING, "Config item missing"},

    /* 日志错误 */
    {ERR_LOG_INIT, "Log initialization failed"},
    {ERR_LOG_FILE, "Log file error"},

    /* 结束标记 */
    {0, NULL}};

/**
 * @brief 获取错误码对应的错误信息
 * @param err_code 错误码
 * @return 错误信息字符串
 */
const char *error_get_string(error_code_t err_code) {
  const error_info_t *info = error_table;

  while (info->message != NULL) {
    if (info->code == err_code) {
      return info->message;
    }
    info++;
  }

  return "Unknown error";
}

/**
 * @brief 打印错误信息
 * @param err_code 错误码
 * @param context 上下文信息
 */
void error_print(error_code_t err_code, const char *context) {
  const char *message = error_get_string(err_code);

  if (context) {
    fprintf(stderr, "[ERROR] %s: %s (code: %d)\n", context, message, err_code);
  } else {
    fprintf(stderr, "[ERROR] %s (code: %d)\n", message, err_code);
  }
}
