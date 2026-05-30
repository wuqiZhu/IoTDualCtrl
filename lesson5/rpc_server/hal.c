/**
 * @file hal.c
 * @brief 硬件抽象层（HAL）实现
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 实现统一的硬件接口，针对i.MX6ULL平台。
 * 更换开发板时只需重写此文件。
 */

#include "hal.h"
#include "dht11.h"
#include "led.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief GPIO导出后等待时间（微秒） */
#define GPIO_EXPORT_DELAY_US 10000

/** @brief DHT11最大重试次数 */
#define DHT11_MAX_RETRIES 10

/** @brief DHT11重试间隔（微秒） */
#define DHT11_RETRY_DELAY_US 1000

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief HAL初始化标志 */
static int hal_initialized = 0;

/** @brief HAL运行时配置 */
static hal_config_t hal_config = {
    .pir_pin = HAL_PIR_PIN,
    .smoke_do_pin = HAL_SMOKE_DO_PIN,
    .relay1_pin = HAL_RELAY1_PIN,
    .relay2_pin = HAL_RELAY2_PIN,
    .light_adc_ch = HAL_LIGHT_ADC_CH,
    .light_threshold = HAL_LIGHT_THRESHOLD,
};

/** @brief 继电器1状态 */
static int relay1_state = 0;

/** @brief 继电器2状态 */
static int relay2_state = 0;

/** @brief 温度滤波缓冲区 */
static int temp_buf[HAL_FILTER_TEMP_WINDOW] = {0};

/** @brief 湿度滤波缓冲区 */
static int humi_buf[HAL_FILTER_HUMI_WINDOW] = {0};

/** @brief 光照ADC滤波缓冲区 */
static int light_buf[HAL_FILTER_LIGHT_WINDOW] = {0};

/** @brief 温度滤波缓冲区索引 */
static int temp_idx = 0;

/** @brief 湿度滤波缓冲区索引 */
static int humi_idx = 0;

/** @brief 光照滤波缓冲区索引 */
static int light_idx = 0;

/** @brief 温度滤波缓冲区填充计数 */
static int temp_count = 0;

/** @brief 湿度滤波缓冲区填充计数 */
static int humi_count = 0;

/** @brief 光照滤波缓冲区填充计数 */
static int light_count = 0;

/** @brief 烟雾防抖 - 上次确认状态 */
static int smoke_confirmed_state = 1;

/** @brief 烟雾防抖 - 连续相同状态计数 */
static int smoke_debounce_count = 0;

/** @brief 烟雾防抖阈值（连续N次读取同一状态才确认） */
#define SMOKE_DEBOUNCE_THRESHOLD 3

/* ========================================================================== */
/*                              GPIO实现 */
/* ========================================================================== */

/**
 * @brief 初始化GPIO子系统
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_init(void) {
  /* GPIO初始化在导出时自动完成 */
  return HAL_OK;
}

/**
 * @brief 导出GPIO引脚
 * @param pin 引脚号
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_export(int pin) {
  char buf[256];
  char path[64];

  /* 检查是否已经导出 */
  sprintf(path, "/sys/class/gpio/gpio%d", pin);
  if (access(path, F_OK) == 0) {
    return HAL_OK; /* 已导出 */
  }

  /* 执行导出 */
  int fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0) {
    printf("hal_gpio_export: failed to open export file, errno=%d\n", errno);
    return HAL_ERROR_OPEN;
  }

  sprintf(buf, "%d", pin);
  if (write(fd, buf, strlen(buf)) < 0) {
    if (errno == EBUSY) {
      close(fd);
      return HAL_OK; /* 已导出 */
    }
    printf("hal_gpio_export: failed to export pin %d, errno=%d\n", pin, errno);
    close(fd);
    return HAL_ERROR_WRITE;
  }

  close(fd);
  usleep(GPIO_EXPORT_DELAY_US);
  return HAL_OK;
}

/**
 * @brief 取消导出GPIO引脚
 * @param pin 引脚号
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_unexport(int pin) {
  char buf[16];
  int fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (fd < 0) {
    return HAL_ERROR_OPEN;
  }
  sprintf(buf, "%d", pin);
  write(fd, buf, strlen(buf));
  close(fd);
  return HAL_OK;
}

/**
 * @brief 设置GPIO方向
 * @param pin 引脚号
 * @param is_output 1输出, 0输入
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_set_direction(int pin, int is_output) {
  char path[256];
  const char *dir = is_output ? "out" : "in";

  sprintf(path, "/sys/class/gpio/gpio%d/direction", pin);

  if (access(path, W_OK) != 0) {
    printf("hal_gpio_set_direction: direction file not accessible for pin %d\n",
           pin);
    return HAL_ERROR_OPEN;
  }

  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("hal_gpio_set_direction: failed to open direction file for pin %d\n",
           pin);
    return HAL_ERROR_OPEN;
  }

  if (write(fd, dir, strlen(dir)) < 0) {
    printf("hal_gpio_set_direction: failed to set direction for pin %d\n", pin);
    close(fd);
    return HAL_ERROR_WRITE;
  }

  close(fd);
  return HAL_OK;
}

/**
 * @brief 读取GPIO值
 * @param pin 引脚号
 * @param value 输出值 (0或1)
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_read(int pin, int *value) {
  char path[256];
  char val = 0;

  sprintf(path, "/sys/class/gpio/gpio%d/value", pin);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("hal_gpio_read: failed to open value file for pin %d\n", pin);
    return HAL_ERROR_OPEN;
  }

  if (read(fd, &val, 1) != 1) {
    printf("hal_gpio_read: failed to read value for pin %d\n", pin);
    close(fd);
    return HAL_ERROR_READ;
  }

  close(fd);
  *value = val - '0';
  return HAL_OK;
}

/**
 * @brief 写入GPIO值
 * @param pin 引脚号
 * @param value 值 (0或1)
 * @return HAL_OK成功
 */
hal_error_t hal_gpio_write(int pin, int value) {
  char path[256];
  char val = value ? '1' : '0';

  sprintf(path, "/sys/class/gpio/gpio%d/value", pin);

  int fd = open(path, O_WRONLY);
  if (fd < 0) {
    printf("hal_gpio_write: failed to open value file for pin %d\n", pin);
    return HAL_ERROR_OPEN;
  }

  if (write(fd, &val, 1) != 1) {
    printf("hal_gpio_write: failed to set value for pin %d\n", pin);
    close(fd);
    return HAL_ERROR_WRITE;
  }

  close(fd);
  return HAL_OK;
}

/* ========================================================================== */
/*                              ADC实现 */
/* ========================================================================== */

/**
 * @brief 初始化ADC子系统
 * @return HAL_OK成功
 */
hal_error_t hal_adc_init(void) {
  /* ADC初始化在读取时自动完成 */
  return HAL_OK;
}

/**
 * @brief 读取ADC原始值
 * @param channel ADC通道号
 * @param value 输出原始值
 * @return HAL_OK成功
 */
hal_error_t hal_adc_read_raw(int channel, int *value) {
  char path[256];
  char buf[16] = {0};

  sprintf(path, "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw", channel);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("hal_adc_read_raw: failed to open %s\n", path);
    return HAL_ERROR_OPEN;
  }

  if (read(fd, buf, sizeof(buf) - 1) < 0) {
    printf("hal_adc_read_raw: failed to read from %s\n", path);
    close(fd);
    return HAL_ERROR_READ;
  }

  close(fd);
  *value = atoi(buf);
  return HAL_OK;
}

/**
 * @brief 读取ADC电压值（毫伏）
 * @param channel ADC通道号
 * @param voltage_mv 输出电压值（毫伏）
 * @return HAL_OK成功
 */
hal_error_t hal_adc_read_voltage(int channel, int *voltage_mv) {
  int raw;
  hal_error_t ret = hal_adc_read_raw(channel, &raw);
  if (ret != HAL_OK) {
    return ret;
  }

  /* 假设参考电压为3.3V，ADC精度为12位 */
  *voltage_mv = (raw * 3300) / 4096;
  return HAL_OK;
}

/* ========================================================================== */
/*                              滤波辅助函数 */
/* ========================================================================== */

/**
 * @brief 滑动平均滤波
 * @param buf 缓冲区
 * @param idx 当前索引指针
 * @param count 已填充计数指针
 * @param size 窗口大小
 * @param new_val 新采样值
 * @return 滤波后的平均值
 */
static int sliding_average(int *buf, int *idx, int *count, int size, int new_val) {
  buf[*idx] = new_val;
  *idx = (*idx + 1) % size;
  if (*count < size) {
    (*count)++;
  }

  int sum = 0;
  for (int i = 0; i < *count; i++) {
    sum += buf[i];
  }
  return sum / *count;
}

/* ========================================================================== */
/*                              传感器实现 */
/* ========================================================================== */

/**
 * @brief 初始化传感器
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_init(void) {
  /* 初始化LED和DHT11驱动 */
  led_init();
  dht11_init();
  
  /* 导出PIR和烟雾传感器GPIO引脚 */
  hal_gpio_export(hal_config.pir_pin);
  hal_gpio_export(hal_config.smoke_do_pin);
  
  /* 设置为输入模式 */
  hal_gpio_set_direction(hal_config.pir_pin, 0);
  hal_gpio_set_direction(hal_config.smoke_do_pin, 0);
  
  return HAL_OK;
}

/**
 * @brief 读取DHT11温湿度传感器
 * @param humidity 输出湿度值
 * @param temperature 输出温度值
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_dht11_read(int *humidity, int *temperature) {
  char humi, temp;
  int retry_count = 0;

  while (0 != dht11_read(&humi, &temp)) {
    retry_count++;
    if (retry_count >= DHT11_MAX_RETRIES) {
      printf("hal_sensor_dht11_read: failed after %d retries\n",
             DHT11_MAX_RETRIES);
      return HAL_ERROR_TIMEOUT;
    }
    usleep(DHT11_RETRY_DELAY_US);
  }

  *humidity = sliding_average(humi_buf, &humi_idx, &humi_count,
                               HAL_FILTER_HUMI_WINDOW, (int)humi);
  *temperature = sliding_average(temp_buf, &temp_idx, &temp_count,
                                  HAL_FILTER_TEMP_WINDOW, (int)temp);
  return HAL_OK;
}

/**
 * @brief 读取PIR人体红外传感器
 * @param value 输出值 (0无人, 1有人)
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_pir_read(int *value) {
  return hal_gpio_read(hal_config.pir_pin, value);
}

/**
 * @brief 读取光敏传感器
 * @param value 输出值 (0明亮, 1黑暗)
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_light_read(int *value) {
  int raw;
  hal_error_t ret = hal_adc_read_raw(hal_config.light_adc_ch, &raw);
  if (ret != HAL_OK) {
    return ret;
  }

  int filtered = sliding_average(light_buf, &light_idx, &light_count,
                                  HAL_FILTER_LIGHT_WINDOW, raw);

  *value = (filtered < hal_config.light_threshold) ? 0 : 1;
  return HAL_OK;
}

/**
 * @brief 读取烟雾传感器（数字）
 * @param value 输出值 (0检测到烟雾, 1正常)
 * @return HAL_OK成功
 */
hal_error_t hal_sensor_smoke_digital_read(int *value) {
  int raw;
  hal_error_t ret = hal_gpio_read(hal_config.smoke_do_pin, &raw);
  if (ret != HAL_OK) {
    return ret;
  }

  if (raw == smoke_confirmed_state) {
    smoke_debounce_count = 0;
  } else {
    smoke_debounce_count++;
    if (smoke_debounce_count >= SMOKE_DEBOUNCE_THRESHOLD) {
      smoke_confirmed_state = raw;
      smoke_debounce_count = 0;
    }
  }

  *value = smoke_confirmed_state;
  return HAL_OK;
}



/* ========================================================================== */
/*                              执行器实现 */
/* ========================================================================== */

/**
 * @brief 初始化执行器
 * @return HAL_OK成功
 */
hal_error_t hal_actuator_init(void) {
  /* 导出继电器引脚 */
  hal_gpio_export(hal_config.relay1_pin);
  hal_gpio_export(hal_config.relay2_pin);

  /* 设置为输出模式 */
  hal_gpio_set_direction(hal_config.relay1_pin, 1);
  hal_gpio_set_direction(hal_config.relay2_pin, 1);

  /* 初始化为关闭状态 */
  hal_gpio_write(hal_config.relay1_pin, 0);
  hal_gpio_write(hal_config.relay2_pin, 0);
  relay1_state = 0;
  relay2_state = 0;

  return HAL_OK;
}

/**
 * @brief 控制LED灯
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_led_control(int on) {
  led_control(on);
  return HAL_OK;
}

/**
 * @brief 控制继电器1（风扇）
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_relay1_control(int on) {
  hal_error_t ret = hal_gpio_write(hal_config.relay1_pin, on);
  if (ret == HAL_OK) {
    relay1_state = on;
  }
  return ret;
}

/**
 * @brief 读取继电器1状态
 * @param state 输出状态 (0关闭, 1打开)
 * @return HAL_OK成功
 */
hal_error_t hal_relay1_read(int *state) {
  return hal_gpio_read(hal_config.relay1_pin, state);
}

/**
 * @brief 控制继电器2（LED灯）
 * @param on 0关闭, 1打开
 * @return HAL_OK成功
 */
hal_error_t hal_relay2_control(int on) {
  hal_error_t ret = hal_gpio_write(hal_config.relay2_pin, on);
  if (ret == HAL_OK) {
    relay2_state = on;
  }
  return ret;
}

/**
 * @brief 读取继电器2状态
 * @param state 输出状态 (0关闭, 1打开)
 * @return HAL_OK成功
 */
hal_error_t hal_relay2_read(int *state) {
  *state = relay2_state;
  return HAL_OK;
}

/* ========================================================================== */
/*                              系统实现 */
/* ========================================================================== */

/**
 * @brief 获取默认HAL配置
 * @param config 输出配置结构体
 */
void hal_get_default_config(hal_config_t *config) {
  if (config) {
    config->pir_pin = HAL_PIR_PIN;
    config->smoke_do_pin = HAL_SMOKE_DO_PIN;
    config->relay1_pin = HAL_RELAY1_PIN;
    config->relay2_pin = HAL_RELAY2_PIN;
    config->light_adc_ch = HAL_LIGHT_ADC_CH;
    config->light_threshold = HAL_LIGHT_THRESHOLD;
  }
}

/**
 * @brief 初始化整个HAL层
 * @param config 硬件配置参数，NULL使用默认配置
 * @return HAL_OK成功
 */
hal_error_t hal_init(const hal_config_t *config) {
  hal_error_t ret;

  /* 使用提供的配置或默认配置 */
  if (config) {
    hal_config = *config;
  }

  printf("HAL init with config:\n");
  printf("  PIR pin: %d\n", hal_config.pir_pin);
  printf("  Smoke DO pin: %d\n", hal_config.smoke_do_pin);
  printf("  Relay1 pin: %d\n", hal_config.relay1_pin);
  printf("  Relay2 pin: %d\n", hal_config.relay2_pin);
  printf("  Light ADC ch: %d\n", hal_config.light_adc_ch);
  printf("  Light threshold: %d\n", hal_config.light_threshold);

  ret = hal_gpio_init();
  if (ret != HAL_OK)
    return ret;

  ret = hal_adc_init();
  if (ret != HAL_OK)
    return ret;

  ret = hal_sensor_init();
  if (ret != HAL_OK)
    return ret;

  ret = hal_actuator_init();
  if (ret != HAL_OK)
    return ret;

  hal_initialized = 1;
  return HAL_OK;
}

/**
 * @brief 清理HAL层资源
 */
hal_error_t hal_cleanup(void) {
  /* 关闭所有继电器 */
  hal_relay1_control(0);
  hal_relay2_control(0);

  /* 取消导出GPIO */
  hal_gpio_unexport(hal_config.pir_pin);
  hal_gpio_unexport(hal_config.smoke_do_pin);
  hal_gpio_unexport(hal_config.relay1_pin);
  hal_gpio_unexport(hal_config.relay2_pin);

  hal_initialized = 0;
  return HAL_OK;
}

/**
 * @brief 获取HAL错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *hal_get_error_string(hal_error_t error) {
  switch (error) {
  case HAL_OK:
    return "Success";
  case HAL_ERROR:
    return "Generic error";
  case HAL_ERROR_INVALID_PIN:
    return "Invalid pin";
  case HAL_ERROR_OPEN:
    return "Failed to open device";
  case HAL_ERROR_READ:
    return "Failed to read";
  case HAL_ERROR_WRITE:
    return "Failed to write";
  case HAL_ERROR_TIMEOUT:
    return "Operation timeout";
  case HAL_ERROR_NOT_INIT:
    return "Not initialized";
  default:
    return "Unknown error";
  }
}
