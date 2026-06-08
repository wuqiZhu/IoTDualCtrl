/**
 * @file config.h
 * @brief 配置文件加载模块
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 提供JSON配置文件的加载和解析功能。
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 配置项最大长度 */
#define CONFIG_MAX_STRING_LEN 256

/** @brief 配置结构体 - MQTT配置 */
typedef struct {
  char host[CONFIG_MAX_STRING_LEN];
  int port;
  char username[CONFIG_MAX_STRING_LEN];
  char password[CONFIG_MAX_STRING_LEN];
  char client_id[CONFIG_MAX_STRING_LEN];
  int clean_session;
} mqtt_config_t;

/** @brief 配置结构体 - MQTT主题 */
typedef struct {
  char command[CONFIG_MAX_STRING_LEN];
  char response[CONFIG_MAX_STRING_LEN];
  char telemetry[CONFIG_MAX_STRING_LEN];
  char alert[CONFIG_MAX_STRING_LEN];
  char heartbeat[CONFIG_MAX_STRING_LEN];
} topics_config_t;

/** @brief 配置结构体 - GPIO引脚 */
typedef struct {
  int pir_pin;
  int smoke_do_pin;
  int relay1_pin;
  int relay2_pin;
} gpio_config_t;

/** @brief 配置结构体 - ADC路径 */
typedef struct {
  char light_path[CONFIG_MAX_STRING_LEN];
} adc_config_t;

/** @brief 配置结构体 - 阈值参数 */
typedef struct {
  int light_threshold;
  int temp_high;
  int temp_low;
  int smoke_alert_level;
  int pir_off_delay;
  int smoke_fan_duration;
  int smoke_alert_interval;
  int temp_change;
  int humi_change;
  int smoke_fault_timeout;
} thresholds_config_t;

/** @brief 配置结构体 - 间隔参数 */
typedef struct {
  int telemetry;
  int dht11_retry_delay_us;
  int dht11_max_retries;
  int mqtt_reconnect_max_retries;
  int mqtt_reconnect_delay;
  int heartbeat;
  int full_report;
} intervals_config_t;

/** @brief 配置结构体 - RPC配置 */
typedef struct {
  char server_host[CONFIG_MAX_STRING_LEN];
  int port;
  int read_timeout_ms;
  int max_retries;
} rpc_config_t;

/** @brief 完整配置结构体 */
typedef struct {
  mqtt_config_t mqtt;
  topics_config_t topics;
  gpio_config_t gpio;
  adc_config_t adc;
  thresholds_config_t thresholds;
  intervals_config_t intervals;
  rpc_config_t rpc;
} app_config_t;

/**
 * @brief 加载配置文件
 * @param filename 配置文件路径
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 */
int config_load(const char *filename, app_config_t *config);

/**
 * @brief 打印配置信息
 * @param config 配置结构体
 */
void config_print(const app_config_t *config);

/**
 * @brief 从环境变量加载配置（兼容模式）
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 *
 * 当配置文件不存在时，从环境变量加载必要配置。
 */
int config_load_from_env(app_config_t *config);

/**
 * @brief 组合加载配置（推荐使用）
 * @param filename 配置文件路径
 * @param config 输出配置结构体
 * @return 0成功, -1失败
 *
 * 加载优先级：环境变量 > 配置文件 > 默认值
 * 1. 首先初始化默认值
 * 2. 然后从配置文件加载（如果存在）
 * 3. 最后用环境变量覆盖（如果设置了）
 */
int config_load_combined(const char *filename, app_config_t *config);

/**
 * @brief 更新单个配置项
 * @param config 配置结构体
 * @param key 配置项名称（如"temp_high"）
 * @param value 新的整数值
 * @return 0成功, -1未找到配置项
 */
int config_update_int(app_config_t *config, const char *key, int value);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
