/**
 * @file msg_queue.h
 * @brief 轻量级消息队列模块
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 提供线程安全的消息队列功能，用于解耦MQTT指令接收和执行。
 * 支持阻塞等待、超时、优先级等特性。
 */

#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              常量定义 */
/* ========================================================================== */

/** @brief 消息队列默认容量 */
#define MSG_QUEUE_DEFAULT_CAPACITY 32

/** @brief 消息最大负载大小（字节） */
#define MSG_PAYLOAD_MAX_SIZE 512

/* ========================================================================== */
/*                              错误码定义 */
/* ========================================================================== */

/** @brief 消息队列错误码 */
typedef enum {
  MSGQ_OK = 0,            /**< 操作成功 */
  MSGQ_ERROR = -1,        /**< 通用错误 */
  MSGQ_ERROR_NOMEM = -2,  /**< 内存不足 */
  MSGQ_ERROR_FULL = -3,   /**< 队列已满 */
  MSGQ_ERROR_EMPTY = -4,  /**< 队列为空 */
  MSGQ_ERROR_TIMEOUT = -5,/**< 操作超时 */
  MSGQ_ERROR_PARAM = -6,  /**< 参数错误 */
} msgq_error_t;

/* ========================================================================== */
/*                              消息类型定义 */
/* ========================================================================== */

/** @brief 消息类型 */
typedef enum {
  MSG_TYPE_CONTROL = 0,   /**< 控制指令 */
  MSG_TYPE_TELEMETRY,     /**< 遥测数据 */
  MSG_TYPE_ALERT,         /**< 告警信息 */
  MSG_TYPE_COMMAND,       /**< 系统命令 */
  MSG_TYPE_OTA,           /**< OTA升级指令 */
} msg_type_t;

/** @brief 消息优先级 */
typedef enum {
  MSG_PRIO_LOW = 0,       /**< 低优先级 */
  MSG_PRIO_NORMAL,        /**< 普通优先级 */
  MSG_PRIO_HIGH,          /**< 高优先级 */
  MSG_PRIO_URGENT,        /**< 紧急优先级 */
} msg_prio_t;

/* ========================================================================== */
/*                              消息结构体 */
/* ========================================================================== */

/** @brief 消息结构体 */
typedef struct {
  msg_type_t type;                    /**< 消息类型 */
  msg_prio_t priority;                /**< 消息优先级 */
  int payload_len;                    /**< 负载长度 */
  char payload[MSG_PAYLOAD_MAX_SIZE]; /**< 负载数据 */
  unsigned int msg_id;                /**< 消息ID（自动递增） */
} msg_t;

/* ========================================================================== */
/*                              消息队列结构体 */
/* ========================================================================== */

/** @brief 消息队列 */
typedef struct {
  msg_t *messages;          /**< 消息数组 */
  int capacity;             /**< 队列容量 */
  int count;                /**< 当前消息数量 */
  int head;                 /**< 队头索引 */
  int tail;                 /**< 队尾索引 */
  unsigned int next_msg_id; /**< 下一个消息ID */
  pthread_mutex_t mutex;    /**< 互斥锁 */
  pthread_cond_t not_empty; /**< 非空条件变量 */
  pthread_cond_t not_full;  /**< 非满条件变量 */
  int closed;               /**< 队列关闭标志 */
} msg_queue_t;

/* ========================================================================== */
/*                              公共接口 */
/* ========================================================================== */

/**
 * @brief 创建消息队列
 * @param capacity 队列容量（0使用默认值）
 * @return 消息队列指针，失败返回NULL
 */
msg_queue_t *msgq_create(int capacity);

/**
 * @brief 销毁消息队列
 * @param queue 消息队列指针
 */
void msgq_destroy(msg_queue_t *queue);

/**
 * @brief 向队列发送消息（阻塞）
 * @param queue 消息队列
 * @param msg 消息指针
 * @param timeout_ms 超时时间（毫秒），-1表示永久等待
 * @return MSGQ_OK成功
 */
msgq_error_t msgq_send(msg_queue_t *queue, const msg_t *msg, int timeout_ms);

/**
 * @brief 从队列接收消息（阻塞）
 * @param queue 消息队列
 * @param msg 输出消息指针
 * @param timeout_ms 超时时间（毫秒），-1表示永久等待
 * @return MSGQ_OK成功
 */
msgq_error_t msgq_receive(msg_queue_t *queue, msg_t *msg, int timeout_ms);

/**
 * @brief 尝试向队列发送消息（非阻塞）
 * @param queue 消息队列
 * @param msg 消息指针
 * @return MSGQ_OK成功, MSGQ_ERROR_FULL队列已满
 */
msgq_error_t msgq_try_send(msg_queue_t *queue, const msg_t *msg);

/**
 * @brief 尝试从队列接收消息（非阻塞）
 * @param queue 消息队列
 * @param msg 输出消息指针
 * @return MSGQ_OK成功, MSGQ_ERROR_EMPTY队列为空
 */
msgq_error_t msgq_try_receive(msg_queue_t *queue, msg_t *msg);

/**
 * @brief 获取队列中的消息数量
 * @param queue 消息队列
 * @return 消息数量，失败返回-1
 */
int msgq_get_count(msg_queue_t *queue);

/**
 * @brief 检查队列是否为空
 * @param queue 消息队列
 * @return 1为空, 0非空
 */
int msgq_is_empty(msg_queue_t *queue);

/**
 * @brief 检查队列是否已满
 * @param queue 消息队列
 * @return 1已满, 0未满
 */
int msgq_is_full(msg_queue_t *queue);

/**
 * @brief 关闭消息队列
 * @param queue 消息队列
 *
 * 关闭后，所有阻塞的发送/接收操作将返回错误。
 */
void msgq_close(msg_queue_t *queue);

/**
 * @brief 获取消息队列错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *msgq_get_error_string(msgq_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* MSG_QUEUE_H */
