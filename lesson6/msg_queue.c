/**
 * @file msg_queue.c
 * @brief 轻量级消息队列模块实现
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 */

#include "msg_queue.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 计算超时时间点
 * @param timeout_ms 超时毫秒数
 * @param ts 输出timespec结构体
 */
static void calc_timeout(int timeout_ms, struct timespec *ts) {
  clock_gettime(CLOCK_REALTIME, ts);
  ts->tv_sec += timeout_ms / 1000;
  ts->tv_nsec += (timeout_ms % 1000) * 1000000;
  if (ts->tv_nsec >= 1000000000) {
    ts->tv_sec++;
    ts->tv_nsec -= 1000000000;
  }
}

/* ========================================================================== */
/*                              公共接口实现 */
/* ========================================================================== */

msg_queue_t *msgq_create(int capacity) {
  if (capacity <= 0) {
    capacity = MSG_QUEUE_DEFAULT_CAPACITY;
  }

  msg_queue_t *queue = (msg_queue_t *)malloc(sizeof(msg_queue_t));
  if (!queue) {
    return NULL;
  }

  queue->messages = (msg_t *)malloc(sizeof(msg_t) * capacity);
  if (!queue->messages) {
    free(queue);
    return NULL;
  }

  queue->capacity = capacity;
  queue->count = 0;
  queue->head = 0;
  queue->tail = 0;
  queue->next_msg_id = 1;
  queue->closed = 0;

  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->not_empty, NULL);
  pthread_cond_init(&queue->not_full, NULL);

  return queue;
}

void msgq_destroy(msg_queue_t *queue) {
  if (!queue) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);
  queue->closed = 1;
  pthread_cond_broadcast(&queue->not_empty);
  pthread_cond_broadcast(&queue->not_full);
  pthread_mutex_unlock(&queue->mutex);

  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);

  free(queue->messages);
  free(queue);
}

msgq_error_t msgq_send(msg_queue_t *queue, const msg_t *msg, int timeout_ms) {
  if (!queue || !msg) {
    return MSGQ_ERROR_PARAM;
  }

  pthread_mutex_lock(&queue->mutex);

  /* 检查队列是否已关闭 */
  if (queue->closed) {
    pthread_mutex_unlock(&queue->mutex);
    return MSGQ_ERROR;
  }

  /* 等待队列有空间 */
  while (queue->count >= queue->capacity) {
    if (queue->closed) {
      pthread_mutex_unlock(&queue->mutex);
      return MSGQ_ERROR;
    }

    if (timeout_ms == 0) {
      pthread_mutex_unlock(&queue->mutex);
      return MSGQ_ERROR_FULL;
    }

    if (timeout_ms < 0) {
      /* 永久等待 */
      pthread_cond_wait(&queue->not_full, &queue->mutex);
    } else {
      /* 带超时等待 */
      struct timespec ts;
      calc_timeout(timeout_ms, &ts);
      int ret = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &ts);
      if (ret == ETIMEDOUT) {
        pthread_mutex_unlock(&queue->mutex);
        return MSGQ_ERROR_TIMEOUT;
      }
    }
  }

  /* 复制消息 */
  memcpy(&queue->messages[queue->tail], msg, sizeof(msg_t));
  queue->messages[queue->tail].msg_id = queue->next_msg_id++;
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->count++;

  /* 通知等待的接收者 */
  pthread_cond_signal(&queue->not_empty);
  pthread_mutex_unlock(&queue->mutex);

  return MSGQ_OK;
}

msgq_error_t msgq_receive(msg_queue_t *queue, msg_t *msg, int timeout_ms) {
  if (!queue || !msg) {
    return MSGQ_ERROR_PARAM;
  }

  pthread_mutex_lock(&queue->mutex);

  /* 检查队列是否已关闭 */
  if (queue->closed && queue->count == 0) {
    pthread_mutex_unlock(&queue->mutex);
    return MSGQ_ERROR;
  }

  /* 等待队列有消息 */
  while (queue->count == 0) {
    if (queue->closed) {
      pthread_mutex_unlock(&queue->mutex);
      return MSGQ_ERROR;
    }

    if (timeout_ms == 0) {
      pthread_mutex_unlock(&queue->mutex);
      return MSGQ_ERROR_EMPTY;
    }

    if (timeout_ms < 0) {
      /* 永久等待 */
      pthread_cond_wait(&queue->not_empty, &queue->mutex);
    } else {
      /* 带超时等待 */
      struct timespec ts;
      calc_timeout(timeout_ms, &ts);
      int ret = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
      if (ret == ETIMEDOUT) {
        pthread_mutex_unlock(&queue->mutex);
        return MSGQ_ERROR_TIMEOUT;
      }
    }
  }

  /* 复制消息 */
  memcpy(msg, &queue->messages[queue->head], sizeof(msg_t));
  queue->head = (queue->head + 1) % queue->capacity;
  queue->count--;

  /* 通知等待的发送者 */
  pthread_cond_signal(&queue->not_full);
  pthread_mutex_unlock(&queue->mutex);

  return MSGQ_OK;
}

msgq_error_t msgq_try_send(msg_queue_t *queue, const msg_t *msg) {
  return msgq_send(queue, msg, 0);
}

msgq_error_t msgq_try_receive(msg_queue_t *queue, msg_t *msg) {
  return msgq_receive(queue, msg, 0);
}

int msgq_get_count(msg_queue_t *queue) {
  if (!queue) {
    return -1;
  }

  pthread_mutex_lock(&queue->mutex);
  int count = queue->count;
  pthread_mutex_unlock(&queue->mutex);

  return count;
}

int msgq_is_empty(msg_queue_t *queue) {
  return msgq_get_count(queue) == 0;
}

int msgq_is_full(msg_queue_t *queue) {
  if (!queue) {
    return 0;
  }

  pthread_mutex_lock(&queue->mutex);
  int full = (queue->count >= queue->capacity);
  pthread_mutex_unlock(&queue->mutex);

  return full;
}

void msgq_close(msg_queue_t *queue) {
  if (!queue) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);
  queue->closed = 1;
  pthread_cond_broadcast(&queue->not_empty);
  pthread_cond_broadcast(&queue->not_full);
  pthread_mutex_unlock(&queue->mutex);
}

const char *msgq_get_error_string(msgq_error_t error) {
  switch (error) {
  case MSGQ_OK:
    return "Success";
  case MSGQ_ERROR:
    return "Generic error";
  case MSGQ_ERROR_NOMEM:
    return "Out of memory";
  case MSGQ_ERROR_FULL:
    return "Queue is full";
  case MSGQ_ERROR_EMPTY:
    return "Queue is empty";
  case MSGQ_ERROR_TIMEOUT:
    return "Operation timeout";
  case MSGQ_ERROR_PARAM:
    return "Invalid parameter";
  default:
    return "Unknown error";
  }
}
