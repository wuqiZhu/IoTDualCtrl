/**
 * @file config.c
 * @brief 配置文件加载模块实现
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 */

#include "config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 初始化默认配置
 * @param config 配置结构体
 */
static void config_init_defaults(app_config_t *config) {
  /* MQTT默认配置 */
  strcpy(config->mqtt.host, "");
  config->mqtt.port = 1883;
  strcpy(config->mqtt.username, "");
  strcpy(config->mqtt.password, "");
  strcpy(config->mqtt.client_id, "mqtt_bridge");
  config->mqtt.clean_session = 1;

  /* 主题默认配置 */
  strcpy(config->topics.command, "device/control");
  strcpy(config->topics.response, "device/response");
  strcpy(config->topics.telemetry, "device/telemetry");
  strcpy(config->topics.alert, "device/alert");

  /* GPIO默认配置 */
  config->gpio.pir_pin = 116;
  config->gpio.smoke_do_pin = 117;
  config->gpio.relay1_pin = 118;
  config->gpio.relay2_pin = 119;

  /* ADC默认配置 */
  strcpy(config->adc.light_path,
         "/sys/bus/iio/devices/iio:device0/in_voltage3_raw");

  /* 阈值默认配置 */
  config->thresholds.light_threshold = 2000;
  config->thresholds.temp_high = 32;
  config->thresholds.temp_low = 30;
  config->thresholds.smoke_alert_level = 0;
  config->thresholds.pir_off_delay = 30;
  config->thresholds.smoke_fan_duration = 30;
  config->thresholds.smoke_alert_interval = 10;

  /* 间隔默认配置 */
  config->intervals.telemetry = 5;
  config->intervals.dht11_retry_delay_us = 1000;
  config->intervals.dht11_max_retries = 10;
  config->intervals.mqtt_reconnect_max_retries = 5;
  config->intervals.mqtt_reconnect_delay = 5;

  /* RPC默认配置 */
  strcpy(config->rpc.server_host, "127.0.0.1");
  config->rpc.port = 1234;
  config->rpc.read_timeout_ms = 3000;
  config->rpc.max_retries = 10;
}

/**
 * @brief 从JSON对象读取字符串
 * @param obj JSON对象
 * @param key 键名
 * @param value 输出缓冲区
 * @param max_len 缓冲区最大长度
 */
static void json_get_string(const cJSON *obj, const char *key, char *value,
                            int max_len) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (item && cJSON_IsString(item) && item->valuestring) {
    strncpy(value, item->valuestring, max_len - 1);
    value[max_len - 1] = '\0';
  }
}

/**
 * @brief 从JSON对象读取整数
 * @param obj JSON对象
 * @param key 键名
 * @param default_value 默认值
 * @return 整数值
 */
static int json_get_int(const cJSON *obj, const char *key, int default_value) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (item && cJSON_IsNumber(item)) {
    return item->valueint;
  }
  return default_value;
}

/**
 * @brief 加载配置文件
 * @param filename 配置文件路径
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 */
int config_load(const char *filename, app_config_t *config) {
  FILE *fp;
  long file_size;
  char *json_string;
  cJSON *root;

  /* 初始化默认值 */
  config_init_defaults(config);

  /* 读取文件 */
  fp = fopen(filename, "r");
  if (!fp) {
    printf("Config file not found: %s, using defaults\n", filename);
    return -1;
  }

  /* 获取文件大小 */
  fseek(fp, 0, SEEK_END);
  file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  /* 分配内存并读取 */
  json_string = (char *)malloc(file_size + 1);
  if (!json_string) {
    fclose(fp);
    printf("Memory allocation failed\n");
    return -1;
  }

  fread(json_string, 1, file_size, fp);
  json_string[file_size] = '\0';
  fclose(fp);

  /* 解析JSON */
  root = cJSON_Parse(json_string);
  free(json_string);

  if (!root) {
    printf("JSON parse error\n");
    return -1;
  }

  /* 读取MQTT配置 */
  cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
  if (mqtt) {
    json_get_string(mqtt, "host", config->mqtt.host, CONFIG_MAX_STRING_LEN);
    config->mqtt.port = json_get_int(mqtt, "port", 1883);
    json_get_string(mqtt, "username", config->mqtt.username,
                    CONFIG_MAX_STRING_LEN);
    json_get_string(mqtt, "password", config->mqtt.password,
                    CONFIG_MAX_STRING_LEN);
    json_get_string(mqtt, "client_id", config->mqtt.client_id,
                    CONFIG_MAX_STRING_LEN);
    config->mqtt.clean_session = json_get_int(mqtt, "clean_session", 1);
  }

  /* 读取主题配置 */
  cJSON *topics = cJSON_GetObjectItem(root, "topics");
  if (topics) {
    json_get_string(topics, "command", config->topics.command,
                    CONFIG_MAX_STRING_LEN);
    json_get_string(topics, "response", config->topics.response,
                    CONFIG_MAX_STRING_LEN);
    json_get_string(topics, "telemetry", config->topics.telemetry,
                    CONFIG_MAX_STRING_LEN);
    json_get_string(topics, "alert", config->topics.alert,
                    CONFIG_MAX_STRING_LEN);
  }

  /* 读取GPIO配置 */
  cJSON *gpio = cJSON_GetObjectItem(root, "gpio");
  if (gpio) {
    config->gpio.pir_pin = json_get_int(gpio, "pir_pin", 116);
    config->gpio.smoke_do_pin = json_get_int(gpio, "smoke_do_pin", 117);
    config->gpio.relay1_pin = json_get_int(gpio, "relay1_pin", 118);
    config->gpio.relay2_pin = json_get_int(gpio, "relay2_pin", 119);
  }

  /* 读取ADC配置 */
  cJSON *adc = cJSON_GetObjectItem(root, "adc");
  if (adc) {
    json_get_string(adc, "light_path", config->adc.light_path,
                    CONFIG_MAX_STRING_LEN);
  }

  /* 读取阈值配置 */
  cJSON *thresholds = cJSON_GetObjectItem(root, "thresholds");
  if (thresholds) {
    config->thresholds.light_threshold =
        json_get_int(thresholds, "light_threshold", 2000);
    config->thresholds.temp_high = json_get_int(thresholds, "temp_high", 32);
    config->thresholds.temp_low = json_get_int(thresholds, "temp_low", 30);
    config->thresholds.smoke_alert_level =
        json_get_int(thresholds, "smoke_alert_level", 0);
    config->thresholds.pir_off_delay =
        json_get_int(thresholds, "pir_off_delay", 30);
    config->thresholds.smoke_fan_duration =
        json_get_int(thresholds, "smoke_fan_duration", 30);
    config->thresholds.smoke_alert_interval =
        json_get_int(thresholds, "smoke_alert_interval", 10);
  }

  /* 读取间隔配置 */
  cJSON *intervals = cJSON_GetObjectItem(root, "intervals");
  if (intervals) {
    config->intervals.telemetry = json_get_int(intervals, "telemetry", 5);
    config->intervals.dht11_retry_delay_us =
        json_get_int(intervals, "dht11_retry_delay_us", 1000);
    config->intervals.dht11_max_retries =
        json_get_int(intervals, "dht11_max_retries", 10);
    config->intervals.mqtt_reconnect_max_retries =
        json_get_int(intervals, "mqtt_reconnect_max_retries", 5);
    config->intervals.mqtt_reconnect_delay =
        json_get_int(intervals, "mqtt_reconnect_delay", 5);
  }

  /* 读取RPC配置 */
  cJSON *rpc = cJSON_GetObjectItem(root, "rpc");
  if (rpc) {
    json_get_string(rpc, "server_host", config->rpc.server_host,
                    CONFIG_MAX_STRING_LEN);
    config->rpc.port = json_get_int(rpc, "port", 1234);
    config->rpc.read_timeout_ms = json_get_int(rpc, "read_timeout_ms", 3000);
    config->rpc.max_retries = json_get_int(rpc, "max_retries", 10);
  }

  cJSON_Delete(root);
  return 0;
}

/**
 * @brief 打印配置信息
 * @param config 配置结构体
 */
void config_print(const app_config_t *config) {
  printf("=== Configuration ===\n");
  printf("MQTT:\n");
  printf("  host: %s\n", config->mqtt.host);
  printf("  port: %d\n", config->mqtt.port);
  printf("  username: %s\n", config->mqtt.username);
  printf("  client_id: %s\n", config->mqtt.client_id);
  printf("\n");
  printf("Topics:\n");
  printf("  command: %s\n", config->topics.command);
  printf("  response: %s\n", config->topics.response);
  printf("  telemetry: %s\n", config->topics.telemetry);
  printf("  alert: %s\n", config->topics.alert);
  printf("\n");
  printf("GPIO:\n");
  printf("  pir_pin: %d\n", config->gpio.pir_pin);
  printf("  smoke_do_pin: %d\n", config->gpio.smoke_do_pin);
  printf("  relay1_pin: %d\n", config->gpio.relay1_pin);
  printf("  relay2_pin: %d\n", config->gpio.relay2_pin);
  printf("\n");
  printf("Thresholds:\n");
  printf("  light_threshold: %d\n", config->thresholds.light_threshold);
  printf("  temp_high: %d\n", config->thresholds.temp_high);
  printf("  temp_low: %d\n", config->thresholds.temp_low);
  printf("  smoke_alert_level: %d\n", config->thresholds.smoke_alert_level);
  printf("  pir_off_delay: %d\n", config->thresholds.pir_off_delay);
  printf("  smoke_fan_duration: %d\n", config->thresholds.smoke_fan_duration);
  printf("  smoke_alert_interval: %d\n", config->thresholds.smoke_alert_interval);
  printf("====================\n");
}

/**
 * @brief 从环境变量加载配置（兼容模式）
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 */
int config_load_from_env(app_config_t *config) {
  const char *env;

  /* 初始化默认值 */
  config_init_defaults(config);

  /* 从环境变量读取MQTT配置 */
  env = getenv("MQTT_HOST");
  if (env) {
    strncpy(config->mqtt.host, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_PORT");
  if (env) {
    config->mqtt.port = atoi(env);
  }

  env = getenv("MQTT_USERNAME");
  if (env) {
    strncpy(config->mqtt.username, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_PASSWORD");
  if (env) {
    strncpy(config->mqtt.password, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_CLIENTID");
  if (env) {
    strncpy(config->mqtt.client_id, env, CONFIG_MAX_STRING_LEN - 1);
  }

  /* 检查必要配置 */
  if (config->mqtt.host[0] == '\0') {
    fprintf(stderr, "Missing configuration: MQTT_HOST\n");
    return -1;
  }
  if (config->mqtt.username[0] == '\0') {
    fprintf(stderr, "Missing configuration: MQTT_USERNAME\n");
    return -1;
  }
  if (config->mqtt.password[0] == '\0') {
    fprintf(stderr, "Missing configuration: MQTT_PASSWORD\n");
    return -1;
  }

  return 0;
}

/**
 * @brief 从环境变量覆盖配置
 * @param config 配置结构体
 *
 * 仅覆盖设置了环境变量的配置项。
 */
static void config_override_from_env(app_config_t *config) {
  const char *env;

  /* MQTT配置 */
  env = getenv("MQTT_HOST");
  if (env) {
    strncpy(config->mqtt.host, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_PORT");
  if (env) {
    config->mqtt.port = atoi(env);
  }

  env = getenv("MQTT_USERNAME");
  if (env) {
    strncpy(config->mqtt.username, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_PASSWORD");
  if (env) {
    strncpy(config->mqtt.password, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("MQTT_CLIENTID");
  if (env) {
    strncpy(config->mqtt.client_id, env, CONFIG_MAX_STRING_LEN - 1);
  }

  /* RPC配置 */
  env = getenv("RPC_HOST");
  if (env) {
    strncpy(config->rpc.server_host, env, CONFIG_MAX_STRING_LEN - 1);
  }

  env = getenv("RPC_PORT");
  if (env) {
    config->rpc.port = atoi(env);
  }
}

/**
 * @brief 组合加载配置（推荐使用）
 * @param filename 配置文件路径
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 *
 * 加载优先级：环境变量 > 配置文件 > 默认值
 */
int config_load_combined(const char *filename, app_config_t *config) {
  /* 1. 初始化默认值 */
  config_init_defaults(config);

  /* 2. 尝试加载配置文件 */
  config_load(filename, config);

  /* 3. 用环境变量覆盖 */
  config_override_from_env(config);

  /* 3. 检查必要配置 */
  if (config->mqtt.host[0] == '\0') {
    fprintf(stderr, "Missing configuration: MQTT_HOST (set via config file or "
                    "environment variable)\n");
    return -1;
  }
  if (config->mqtt.username[0] == '\0') {
    fprintf(stderr, "Missing configuration: MQTT_USERNAME (set via config file "
                    "or environment variable)\n");
    return -1;
  }

  return 0;
}
