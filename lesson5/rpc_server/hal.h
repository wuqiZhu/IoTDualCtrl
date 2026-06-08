/**
 * @file hal.h
 * @brief 硬件抽象层（HAL）接口定义
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 定义统一的硬件接口，将硬件相关的代码与业务逻辑分离。
 * 更换开发板时只需重写 hal.c 实现。
 */

#ifndef HAL_H
#define HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

/* ========================================================================== */
/*                              硬件引脚配置 */
/* ========================================================================== */

/** @brief PIR人体红外传感器引脚 */
#define HAL_PIR_PIN 116

/** @brief 烟雾传感器数字输出引脚 */
#define HAL_SMOKE_DO_PIN 117

/** @brief 继电器1控制引脚（风扇） */
#define HAL_RELAY1_PIN 118

/** @brief 继电器2控制引脚（LED灯） */
#define HAL_RELAY2_PIN 119

/** @brief 光敏传感器ADC通道 */
#define HAL_LIGHT_ADC_CH 3

/** @brief 光照阈值（ADC值低于此值为黑暗） */
#define HAL_LIGHT_THRESHOLD 2000

/* ========================================================================== */
/*                              运行时配置结构体 */
/* ========================================================================== */

/** @brief HAL硬件配置结构体 */
typedef struct {
  int pir_pin;           /**< PIR人体红外传感器引脚 */
  int smoke_do_pin;      /**< 烟雾传感器数字输出引脚 */
  int relay1_pin;        /**< 继电器1控制引脚（风扇） */
  int relay2_pin;        /**< 继电器2控制引脚（LED灯） */
  int light_adc_ch;      /**< 光敏传感器ADC通道 */
  int light_threshold;   /**< 光照阈值（ADC值低于此值为黑暗） */
} hal_config_t;

/* ========================================================================== */
/*                              滤波参数配置 */
/* ========================================================================== */

/** @brief 温度滑动平均窗口大小 */
#define HAL_FILTER_TEMP_WINDOW  3

/** @brief 湿度滑动平均窗口大小 */
#define HAL_FILTER_HUMI_WINDOW  3

/** @brief 光照ADC滑动平均窗口大小 */
#define HAL_FILTER_LIGHT_WINDOW 5

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief HAL错误码 */
typedef enum {
  HAL_OK = 0,                    /**< 操作成功 */
  HAL_ERROR = -1,                /**< 通用错误 */
  HAL_ERROR_INVALID_PIN = -2,    /**< 无效引脚 */
  HAL_ERROR_OPEN = -3,           /**< 打开设备失败 */
  HAL_ERROR_READ = -4,           /**< 读取失败 */
  HAL_ERROR_WRITE = -5,          /**< 写入失败 */
  HAL_ERROR_TIMEOUT = -6,        /**< 操作超时 */
  HAL_ERROR_NOT_INIT = -7,       /**< 未初始化 */
  HAL_ERROR_SENSOR_OFFLINE = -8, /**< 传感器离线（连续失败超过阈值） */
} hal_error_t;

/* ========================================================================== */
/*                              GPIO接口 */
/* ========================================================================== */

/**
 * @brief 初始化GPIO子系统
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_init(void);

/**
 * @brief 导出GPIO引脚
 * @param pin 引脚号
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_export(int pin);

/**
 * @brief 取消导出GPIO引脚
 * @param pin 引脚号
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_unexport(int pin);

/**
 * @brief 设置GPIO方向
 * @param pin 引脚号
 * @param is_output 1输出, 0输入
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_set_direction(int pin, int is_output);

/**
 * @brief 读取GPIO值
 * @param pin 引脚号
 * @param value 输出值 (0或1)
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_read(int pin, int *value);

/**
 * @brief 写入GPIO值
 * @param pin 引脚号
 * @param value 值 (0或1)
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_write(int pin, int value);

/* ========================================================================== */
/*                              ADC接口 */
/* ========================================================================== */

/**
 * @brief 初始化ADC子系统
 * @return HAL_OK成功
 */
hal_error_t hal_adc_init(void);

/**
 * @brief 读取ADC原始值
 * @param channel ADC通道号
 * @param value 输出原始值
 * @return HAL_OK成功
 */
hal_error_t hal_adc_read_raw(int channel, int *value);

/**
 * @brief 读取ADC电压值（毫伏）
 * @param channel ADC通道号
 * @param voltage_mv 输出电压值（毫伏）
 * @return HAL_OK成功
 */
hal_error_t hal_adc_read_voltage(int channel, int *voltage_mv);

/* ========================================================================== */
/*                              传感器故障恢复配置 */
/* ========================================================================== */

/** @brief 传感器连续失败次数阈值，超过此值标记为离线 */
#define HAL_SENSOR_FAILURE_THRESHOLD 5

/** @brief 传感器离线后重试间隔（秒） */
#define HAL_SENSOR_RETRY_INTERVAL 60

/* ========================================================================== */
/*                              传感器状态定义 */
/* ========================================================================== */

/** @brief 传感器ID */
typedef enum {
  HAL_SENSOR_DHT11 = 0,   /**< DHT11温湿度传感器 */
  HAL_SENSOR_PIR,          /**< PIR人体红外传感器 */
  HAL_SENSOR_LIGHT,        /**< 光敏传感器 */
  HAL_SENSOR_SMOKE,        /**< 烟雾传感器 */
  HAL_SENSOR_COUNT         /**< 传感器总数 */
} hal_sensor_id_t;

/** @brief 传感器状态 */
typedef enum {
  HAL_SENSOR_STATUS_ONLINE = 0,   /**< 在线 */
  HAL_SENSOR_STATUS_OFFLINE,      /**< 离线（连续失败超过阈值） */
  HAL_SENSOR_STATUS_UNKNOWN       /**< 未知（未初始化） */
} hal_sensor_status_t;

/** @brief 传感器状态信息结构体 */
typedef struct {
  hal_sensor_status_t status;     /**< 当前状态 */
  int failure_count;              /**< 连续失败次数 */
  time_t last_success_time;       /**< 最后成功时间 */
  time_t last_failure_time;       /**< 最后失败时间 */
  hal_error_t last_error;         /**< 最后一次错误码 */
} hal_sensor_info_t;

/**
 * @brief 初始化传感器
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_init(void);

/**
 * @brief 读取DHT11温湿度传感器
 * @param humidity 输出湿度值
 * @param temperature 输出温度值
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_dht11_read(int *humidity, int *temperature);

/**
 * @brief 读取PIR人体红外传感器
 * @param value 输出值 (0无人, 1有人)
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_pir_read(int *value);

/**
 * @brief 读取光敏传感器
 * @param value 输出值 (0明亮, 1黑暗)
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_light_read(int *value);

/**
 * @brief 读取烟雾传感器（数字）
 * @param value 输出值 (0检测到烟雾, 1正常)
 * @return HAL_OK成功, HAL_ERROR_SENSOR_OFFLINE传感器离线
 */
hal_error_t hal_sensor_smoke_digital_read(int *value);

/**
 * @brief 获取传感器状态信息
 * @param sensor_id 传感器ID
 * @param info 输出状态信息结构体
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_get_status(hal_sensor_id_t sensor_id, hal_sensor_info_t *info);

/* ========================================================================== */
/*                              执行器接口 */
/* ========================================================================== */

/**
 * @brief 初始化执行器
 * @return HAL_OK成功
 */
hal_error_t hal_actuator_init(void);

/**
 * @brief 控制LED灯
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_led_control(int on);

/**
 * @brief 控制继电器1（风扇）
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_relay1_control(int on);

/**
 * @brief 读取继电器1状态
 * @param state 输出状态 (0关闭, 1打开)
 * @return HAL_OK成功
 */
hal_error_t hal_relay1_read(int *state);

/**
 * @brief 控制继电器2（LED灯）
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_relay2_control(int on);

/**
 * @brief 读取继电器2状态
 * @param state 输出状态 (0关闭, 1打开)
 * @return HAL_OK成功
 */
hal_error_t hal_relay2_read(int *state);

/* ========================================================================== */
/*                              系统接口 */
/* ========================================================================== */

/**
 * @brief 获取默认HAL配置
 * @param config 输出配置结构体
 *
 * 使用宏定义的默认值填充配置结构体。
 */
void hal_get_default_config(hal_config_t *config);

/**
 * @brief 初始化整个HAL层
 * @param config 硬件配置参数，NULL使用默认配置
 * @return HAL_OK成功
 */
hal_error_t hal_init(const hal_config_t *config);

/**
 * @brief 清理HAL层资源
 */
hal_error_t hal_cleanup(void);

/**
 * @brief 获取HAL错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *hal_get_error_string(hal_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* HAL_H */
