/**
 * @file sensor_manager.c
 * @brief 传感器管理器模块实现
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 */

#include "sensor_manager.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief 传感器数组 */
static sensor_info_t sensors[SENSOR_MAX_COUNT];

/** @brief 已注册传感器数量 */
static int sensor_count = 0;

/** @brief 管理器初始化标志 */
static int mgr_initialized = 0;

/** @brief 互斥锁 */
static pthread_mutex_t mgr_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 查找传感器索引
 * @param name 传感器名称
 * @return 索引，-1表示未找到
 */
static int find_sensor(const char *name) {
  for (int i = 0; i < sensor_count; i++) {
    if (strcmp(sensors[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/* ========================================================================== */
/*                              公共接口实现 */
/* ========================================================================== */

smgr_error_t sensor_mgr_init(void) {
  pthread_mutex_lock(&mgr_mutex);

  if (mgr_initialized) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_OK;
  }

  memset(sensors, 0, sizeof(sensors));
  sensor_count = 0;
  mgr_initialized = 1;

  pthread_mutex_unlock(&mgr_mutex);
  printf("Sensor manager initialized\n");
  return SMGR_OK;
}

void sensor_mgr_cleanup(void) {
  pthread_mutex_lock(&mgr_mutex);

  for (int i = 0; i < sensor_count; i++) {
    if (sensors[i].ops.close) {
      sensors[i].ops.close();
    }
  }

  sensor_count = 0;
  mgr_initialized = 0;

  pthread_mutex_unlock(&mgr_mutex);
  printf("Sensor manager cleaned up\n");
}

smgr_error_t sensor_mgr_register(const char *name, const char *unit,
                                  sensor_type_t type, const sensor_ops_t *ops) {
  if (!name || !ops) {
    return SMGR_ERROR;
  }

  pthread_mutex_lock(&mgr_mutex);

  if (!mgr_initialized) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR;
  }

  /* 检查是否已注册 */
  if (find_sensor(name) >= 0) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR;  /* 已存在 */
  }

  /* 检查是否已满 */
  if (sensor_count >= SENSOR_MAX_COUNT) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR_FULL;
  }

  /* 注册传感器 */
  sensor_info_t *s = &sensors[sensor_count];
  memset(s, 0, sizeof(sensor_info_t));

  strncpy(s->name, name, SENSOR_NAME_MAX_LEN - 1);
  if (unit) {
    strncpy(s->unit, unit, SENSOR_UNIT_MAX_LEN - 1);
  }
  s->type = type;
  memcpy(&s->ops, ops, sizeof(sensor_ops_t));
  s->enabled = 1;
  s->failure_count = 0;

  /* 初始化传感器 */
  if (s->ops.init && s->ops.init() != 0) {
    printf("Warning: Sensor '%s' init failed, registered but disabled\n", name);
    s->enabled = 0;
  }

  sensor_count++;
  pthread_mutex_unlock(&mgr_mutex);

  printf("Sensor registered: %s (%s)\n", name, unit ? unit : "");
  return SMGR_OK;
}

smgr_error_t sensor_mgr_unregister(const char *name) {
  if (!name) {
    return SMGR_ERROR;
  }

  pthread_mutex_lock(&mgr_mutex);

  int idx = find_sensor(name);
  if (idx < 0) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR_NOT_FOUND;
  }

  /* 关闭传感器 */
  if (sensors[idx].ops.close) {
    sensors[idx].ops.close();
  }

  /* 移动数组 */
  for (int i = idx; i < sensor_count - 1; i++) {
    memcpy(&sensors[i], &sensors[i + 1], sizeof(sensor_info_t));
  }
  sensor_count--;

  pthread_mutex_unlock(&mgr_mutex);
  printf("Sensor unregistered: %s\n", name);
  return SMGR_OK;
}

smgr_error_t sensor_mgr_read(const char *name, sensor_data_t *data) {
  if (!name || !data) {
    return SMGR_ERROR;
  }

  pthread_mutex_lock(&mgr_mutex);

  int idx = find_sensor(name);
  if (idx < 0) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR_NOT_FOUND;
  }

  sensor_info_t *s = &sensors[idx];

  if (!s->enabled) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR;
  }

  if (!s->ops.read) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR;
  }

  /* 读取数据 */
  int ret = s->ops.read(data);
  if (ret == 0) {
    s->failure_count = 0;
    s->last_data = *data;
    s->last_read_time = time(NULL);
  } else {
    s->failure_count++;
    data->valid = 0;
  }

  pthread_mutex_unlock(&mgr_mutex);
  return (ret == 0) ? SMGR_OK : SMGR_ERROR_READ;
}

int sensor_mgr_read_all(char names[][SENSOR_NAME_MAX_LEN],
                        sensor_data_t *data, int count) {
  if (!names || !data || count <= 0) {
    return 0;
  }

  int success = 0;
  for (int i = 0; i < count && i < sensor_count; i++) {
    if (sensor_mgr_read(names[i], &data[i]) == SMGR_OK) {
      success++;
    }
  }
  return success;
}

smgr_error_t sensor_mgr_set_enabled(const char *name, int enabled) {
  if (!name) {
    return SMGR_ERROR;
  }

  pthread_mutex_lock(&mgr_mutex);

  int idx = find_sensor(name);
  if (idx < 0) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR_NOT_FOUND;
  }

  sensors[idx].enabled = enabled ? 1 : 0;

  pthread_mutex_unlock(&mgr_mutex);
  return SMGR_OK;
}

smgr_error_t sensor_mgr_get_info(const char *name, sensor_info_t *info) {
  if (!name || !info) {
    return SMGR_ERROR;
  }

  pthread_mutex_lock(&mgr_mutex);

  int idx = find_sensor(name);
  if (idx < 0) {
    pthread_mutex_unlock(&mgr_mutex);
    return SMGR_ERROR_NOT_FOUND;
  }

  memcpy(info, &sensors[idx], sizeof(sensor_info_t));

  pthread_mutex_unlock(&mgr_mutex);
  return SMGR_OK;
}

int sensor_mgr_get_count(void) {
  pthread_mutex_lock(&mgr_mutex);
  int count = sensor_count;
  pthread_mutex_unlock(&mgr_mutex);
  return count;
}

const char *sensor_mgr_get_error_string(smgr_error_t error) {
  switch (error) {
  case SMGR_OK:
    return "Success";
  case SMGR_ERROR:
    return "Generic error";
  case SMGR_ERROR_NOMEM:
    return "Out of memory";
  case SMGR_ERROR_FULL:
    return "Maximum sensor count reached";
  case SMGR_ERROR_NOT_FOUND:
    return "Sensor not found";
  case SMGR_ERROR_INIT:
    return "Sensor initialization failed";
  case SMGR_ERROR_READ:
    return "Sensor read failed";
  default:
    return "Unknown error";
  }
}
