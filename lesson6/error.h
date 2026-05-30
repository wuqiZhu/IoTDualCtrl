/**
 * @file error.h
 * @brief 错误处理框架
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 定义统一的错误码和错误处理宏。
 */

#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 错误码枚举 */
typedef enum {
  /* 成功 */
  ERR_SUCCESS = 0, /**< 操作成功 */

  /* 通用错误 */
  ERR_INVALID_PARAM = -1,   /**< 无效参数 */
  ERR_NULL_POINTER = -2,    /**< 空指针 */
  ERR_OUT_OF_MEMORY = -3,   /**< 内存不足 */
  ERR_TIMEOUT = -4,         /**< 操作超时 */
  ERR_NOT_FOUND = -5,       /**< 未找到 */
  ERR_ALREADY_EXISTS = -6,  /**< 已存在 */
  ERR_NOT_INITIALIZED = -7, /**< 未初始化 */
  ERR_BUSY = -8,            /**< 忙碌 */
  ERR_ABORTED = -9,         /**< 操作中止 */

  /* GPIO错误 */
  ERR_GPIO_EXPORT = -100,      /**< GPIO导出失败 */
  ERR_GPIO_DIRECTION = -101,   /**< GPIO方向设置失败 */
  ERR_GPIO_READ = -102,        /**< GPIO读取失败 */
  ERR_GPIO_WRITE = -103,       /**< GPIO写入失败 */
  ERR_GPIO_INVALID_PIN = -104, /**< 无效的GPIO引脚 */

  /* ADC错误 */
  ERR_ADC_OPEN = -200, /**< ADC设备打开失败 */
  ERR_ADC_READ = -201, /**< ADC读取失败 */

  /* 传感器错误 */
  ERR_SENSOR_DHT11 = -300, /**< DHT11读取失败 */
  ERR_SENSOR_PIR = -301,   /**< PIR读取失败 */
  ERR_SENSOR_LIGHT = -302, /**< 光敏读取失败 */
  ERR_SENSOR_SMOKE = -303, /**< 烟雾读取失败 */

  /* 继电器错误 */
  ERR_RELAY_CONTROL = -400, /**< 继电器控制失败 */
  ERR_RELAY_READ = -401,    /**< 继电器状态读取失败 */

  /* 网络错误 */
  ERR_SOCKET_CREATE = -500,  /**< Socket创建失败 */
  ERR_SOCKET_CONNECT = -501, /**< Socket连接失败 */
  ERR_SOCKET_SEND = -502,    /**< Socket发送失败 */
  ERR_SOCKET_RECV = -503,    /**< Socket接收失败 */
  ERR_SOCKET_TIMEOUT = -504, /**< Socket超时 */

  /* RPC错误 */
  ERR_RPC_INIT = -600,  /**< RPC初始化失败 */
  ERR_RPC_CALL = -601,  /**< RPC调用失败 */
  ERR_RPC_PARSE = -602, /**< RPC解析失败 */

  /* MQTT错误 */
  ERR_MQTT_INIT = -700,       /**< MQTT初始化失败 */
  ERR_MQTT_CONNECT = -701,    /**< MQTT连接失败 */
  ERR_MQTT_SUBSCRIBE = -702,  /**< MQTT订阅失败 */
  ERR_MQTT_PUBLISH = -703,    /**< MQTT发布失败 */
  ERR_MQTT_DISCONNECT = -704, /**< MQTT断开连接 */

  /* 配置错误 */
  ERR_CONFIG_FILE = -800,    /**< 配置文件错误 */
  ERR_CONFIG_PARSE = -801,   /**< 配置解析错误 */
  ERR_CONFIG_MISSING = -802, /**< 配置项缺失 */

  /* 日志错误 */
  ERR_LOG_INIT = -900, /**< 日志初始化失败 */
  ERR_LOG_FILE = -901, /**< 日志文件错误 */
} error_code_t;

/* ========================================================================== */
/*                              错误处理宏 */
/* ========================================================================== */

/**
 * @brief 检查返回值，如果失败则返回错误码
 * @param expr 表达式
 * @return 如果expr < 0，返回expr
 */
#define CHECK_ERROR(expr)                                                      \
  do {                                                                         \
    int _err = (expr);                                                         \
    if (_err < 0) {                                                            \
      return _err;                                                             \
    }                                                                          \
  } while (0)

/**
 * @brief 检查返回值，如果失败则执行指定操作
 * @param expr 表达式
 * @param action 失败时执行的操作
 */
#define CHECK_ERROR_OR(expr, action)                                           \
  do {                                                                         \
    int _err = (expr);                                                         \
    if (_err < 0) {                                                            \
      action;                                                                  \
    }                                                                          \
  } while (0)

/**
 * @brief 检查指针是否为空
 * @param ptr 指针
 * @return 如果ptr为NULL，返回ERR_NULL_POINTER
 */
#define CHECK_NULL(ptr)                                                        \
  do {                                                                         \
    if ((ptr) == NULL) {                                                       \
      return ERR_NULL_POINTER;                                                 \
    }                                                                          \
  } while (0)

/**
 * @brief 检查指针是否为空，如果为空则执行指定操作
 * @param ptr 指针
 * @param action 为空时执行的操作
 */
#define CHECK_NULL_OR(ptr, action)                                             \
  do {                                                                         \
    if ((ptr) == NULL) {                                                       \
      action;                                                                  \
    }                                                                          \
  } while (0)

/**
 * @brief 检查条件是否满足
 * @param cond 条件
 * @param err_code 不满足时返回的错误码
 * @return 如果!cond，返回err_code
 */
#define CHECK_CONDITION(cond, err_code)                                        \
  do {                                                                         \
    if (!(cond)) {                                                             \
      return (err_code);                                                       \
    }                                                                          \
  } while (0)

/**
 * @brief 检查条件是否满足，如果不满足则执行指定操作
 * @param cond 条件
 * @param err_code 不满足时的错误码
 * @param action 不满足时执行的操作
 */
#define CHECK_CONDITION_OR(cond, err_code, action)                             \
  do {                                                                         \
    if (!(cond)) {                                                             \
      action;                                                                  \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/*                              函数声明 */
/* ========================================================================== */

/**
 * @brief 获取错误码对应的错误信息
 * @param err_code 错误码
 * @return 错误信息字符串
 */
const char *error_get_string(error_code_t err_code);

/**
 * @brief 打印错误信息
 * @param err_code 错误码
 * @param context 上下文信息
 */
void error_print(error_code_t err_code, const char *context);

#ifdef __cplusplus
}
#endif

#endif /* ERROR_H */
