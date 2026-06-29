/**
 * @file ota_manager.c
 * @brief OTA远程升级管理模块实现
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 实现OTA远程升级功能，包括固件下载、校验、备份、安装和回滚。
 * 注意：固件下载需要curl库支持，嵌入式环境可使用wget替代。
 */

#include "ota_manager.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部变量 */
/* ========================================================================== */

/** @brief OTA状态 */
static ota_status_t ota_status = {
    .state = OTA_STATE_IDLE,
    .progress = 0,
    .current_version = {0},
    .target_version = {0},
    .error_msg = {0},
    .retry_count = 0,
};

/** @brief 互斥锁 */
static pthread_mutex_t ota_mutex = PTHREAD_MUTEX_INITIALIZER;

/** @brief 升级线程 */
static pthread_t ota_thread;

/** @brief 升级进行中标志 */
static int ota_upgrading = 0;

/** @brief 固件版本（编译时定义） */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "3.0.0"
#endif

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 保存OTA状态到文件
 */
static void save_ota_state(void) {
  cJSON *root = cJSON_CreateObject();
  if (!root) return;

  cJSON_AddNumberToObject(root, "state", ota_status.state);
  cJSON_AddNumberToObject(root, "progress", ota_status.progress);
  cJSON_AddStringToObject(root, "current_version", ota_status.current_version);
  cJSON_AddStringToObject(root, "target_version", ota_status.target_version);
  cJSON_AddStringToObject(root, "error_msg", ota_status.error_msg);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!json_str) return;

  FILE *fp = fopen(OTA_STATE_FILE, "w");
  if (fp) {
    fwrite(json_str, 1, strlen(json_str), fp);
    fclose(fp);
  }
  free(json_str);
}

/**
 * @brief 计算文件MD5校验和
 * @param file_path 文件路径
 * @param md5_out 输出MD5字符串（32字符+结束符）
 * @return 0成功, -1失败
 *
 * 使用系统命令md5sum计算校验和，适用于嵌入式Linux环境。
 */
static int calculate_md5(const char *file_path, char *md5_out) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "md5sum %s", file_path);

  FILE *fp = popen(cmd, "r");
  if (!fp) {
    return -1;
  }

  if (fscanf(fp, "%32s", md5_out) != 1) {
    pclose(fp);
    return -1;
  }

  pclose(fp);
  return 0;
}

/**
 * @brief 计算文件SHA256校验和
 * @param file_path 文件路径
 * @param sha256_out 输出SHA256字符串（64字符+结束符）
 * @return 0成功, -1失败
 */
static int calculate_sha256(const char *file_path, char *sha256_out) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "sha256sum %s", file_path);

  FILE *fp = popen(cmd, "r");
  if (!fp) {
    return -1;
  }

  if (fscanf(fp, "%64s", sha256_out) != 1) {
    pclose(fp);
    return -1;
  }

  pclose(fp);
  return 0;
}

/**
 * @brief 下载固件
 * @param url 下载URL
 * @param output_path 输出路径
 * @return 0成功, -1失败
 *
 * 使用wget下载固件，适用于嵌入式Linux环境（无需curl库）。
 */
static int download_firmware(const char *url, const char *output_path) {
  char cmd[OTA_URL_MAX_LEN + 128];
  snprintf(cmd, sizeof(cmd), "wget -q -O %s '%s' 2>/dev/null", output_path, url);

  int ret = system(cmd);
  if (ret != 0) {
    printf("OTA: Download failed from %s\n", url);
    return -1;
  }

  /* 检查文件是否存在且大小>0 */
  struct stat st;
  if (stat(output_path, &st) != 0 || st.st_size == 0) {
    printf("OTA: Downloaded file is empty or missing\n");
    return -1;
  }

  printf("OTA: Downloaded %ld bytes\n", st.st_size);
  return 0;
}

/**
 * @brief 验证固件校验和
 * @param file_path 固件文件路径
 * @param expected_checksum 期望的校验和
 * @param checksum_type 校验和类型（0=MD5, 1=SHA256）
 * @return 0成功, -1失败
 */
static int verify_checksum(const char *file_path, const char *expected_checksum,
                           int checksum_type) {
  char calculated[65] = {0};

  if (checksum_type == 0) {
    if (calculate_md5(file_path, calculated) != 0) {
      return -1;
    }
  } else {
    if (calculate_sha256(file_path, calculated) != 0) {
      return -1;
    }
  }

  printf("OTA: Expected checksum: %s\n", expected_checksum);
  printf("OTA: Calculated checksum: %s\n", calculated);

  if (strcasecmp(calculated, expected_checksum) != 0) {
    printf("OTA: Checksum mismatch!\n");
    return -1;
  }

  printf("OTA: Checksum verified\n");
  return 0;
}

/**
 * @brief 备份当前固件
 * @return 0成功, -1失败
 */
static int backup_current_firmware(void) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cp -f %s %s 2>/dev/null", OTA_CURRENT_PATH,
           OTA_BACKUP_PATH);

  int ret = system(cmd);
  if (ret != 0) {
    printf("OTA: Backup failed\n");
    return -1;
  }

  printf("OTA: Current firmware backed up\n");
  return 0;
}

/**
 * @brief 安装新固件
 * @return 0成功, -1失败
 */
static int install_firmware(void) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cp -f %s %s && chmod +x %s", OTA_DOWNLOAD_PATH,
           OTA_CURRENT_PATH, OTA_CURRENT_PATH);

  int ret = system(cmd);
  if (ret != 0) {
    printf("OTA: Installation failed\n");
    return -1;
  }

  printf("OTA: New firmware installed\n");
  return 0;
}

/**
 * @brief 重启服务
 */
static void restart_service(void) {
  printf("OTA: Restarting service...\n");

  /* 保存状态 */
  ota_status.state = OTA_STATE_REBOOTING;
  ota_status.progress = 100;
  save_ota_state();

  /* 重启mqtt_bridge服务 */
  system("systemctl restart mqtt_bridge &");
}

/**
 * @brief OTA升级线程函数
 * @param arg OTA信息指针
 * @return NULL
 */
static void *ota_upgrade_thread(void *arg) {
  ota_info_t *info = (ota_info_t *)arg;
  if (!info) {
    ota_upgrading = 0;
    return NULL;
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.state = OTA_STATE_DOWNLOADING;
  ota_status.progress = 0;
  strncpy(ota_status.target_version, info->version, OTA_VERSION_MAX_LEN - 1);
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  /* 步骤1：下载固件 */
  printf("OTA: Step 1/4 - Downloading firmware...\n");
  if (download_firmware(info->url, OTA_DOWNLOAD_PATH) != 0) {
    pthread_mutex_lock(&ota_mutex);
    ota_status.state = OTA_STATE_FAILED;
    strncpy(ota_status.error_msg, "Download failed", sizeof(ota_status.error_msg) - 1);
    save_ota_state();
    pthread_mutex_unlock(&ota_mutex);
    free(info);
    ota_upgrading = 0;
    return NULL;
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.progress = 30;
  ota_status.state = OTA_STATE_VERIFYING;
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  /* 步骤2：校验固件 */
  printf("OTA: Step 2/4 - Verifying firmware...\n");
  if (strlen(info->checksum) > 0) {
    if (verify_checksum(OTA_DOWNLOAD_PATH, info->checksum, info->checksum_type) != 0) {
      pthread_mutex_lock(&ota_mutex);
      ota_status.state = OTA_STATE_FAILED;
      strncpy(ota_status.error_msg, "Checksum mismatch", sizeof(ota_status.error_msg) - 1);
      save_ota_state();
      pthread_mutex_unlock(&ota_mutex);
      unlink(OTA_DOWNLOAD_PATH);
      free(info);
      ota_upgrading = 0;
      return NULL;
    }
  } else {
    printf("OTA: No checksum provided, skipping verification\n");
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.progress = 50;
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  /* 步骤3：备份当前固件 */
  printf("OTA: Step 3/4 - Backing up current firmware...\n");
  if (backup_current_firmware() != 0) {
    pthread_mutex_lock(&ota_mutex);
    ota_status.state = OTA_STATE_FAILED;
    strncpy(ota_status.error_msg, "Backup failed", sizeof(ota_status.error_msg) - 1);
    save_ota_state();
    pthread_mutex_unlock(&ota_mutex);
    unlink(OTA_DOWNLOAD_PATH);
    free(info);
    ota_upgrading = 0;
    return NULL;
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.progress = 70;
  ota_status.state = OTA_STATE_INSTALLING;
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  /* 步骤4：安装新固件 */
  printf("OTA: Step 4/4 - Installing new firmware...\n");
  if (install_firmware() != 0) {
    printf("OTA: Installation failed, rolling back...\n");
    /* 回滚 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cp -f %s %s", OTA_BACKUP_PATH, OTA_CURRENT_PATH);
    system(cmd);

    pthread_mutex_lock(&ota_mutex);
    ota_status.state = OTA_STATE_FAILED;
    strncpy(ota_status.error_msg, "Install failed, rolled back", sizeof(ota_status.error_msg) - 1);
    save_ota_state();
    pthread_mutex_unlock(&ota_mutex);
    unlink(OTA_DOWNLOAD_PATH);
    free(info);
    ota_upgrading = 0;
    return NULL;
  }

  /* 升级成功 */
  pthread_mutex_lock(&ota_mutex);
  ota_status.state = OTA_STATE_SUCCESS;
  ota_status.progress = 100;
  strncpy(ota_status.current_version, info->version, OTA_VERSION_MAX_LEN - 1);
  strncpy(ota_status.error_msg, "", sizeof(ota_status.error_msg));
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  printf("OTA: Upgrade successful! New version: %s\n", info->version);

  /* 清理下载文件 */
  unlink(OTA_DOWNLOAD_PATH);

  /* 重启服务 */
  restart_service();

  free(info);
  ota_upgrading = 0;
  return NULL;
}

/* ========================================================================== */
/*                              公共接口实现 */
/* ========================================================================== */

ota_error_t ota_init(void) {
  pthread_mutex_lock(&ota_mutex);

  strncpy(ota_status.current_version, FIRMWARE_VERSION, OTA_VERSION_MAX_LEN - 1);
  ota_status.state = OTA_STATE_IDLE;
  ota_status.progress = 0;
  ota_upgrading = 0;

  pthread_mutex_unlock(&ota_mutex);

  printf("OTA manager initialized, firmware version: %s\n", FIRMWARE_VERSION);
  return OTA_OK;
}

void ota_cleanup(void) {
  /* 等待升级线程结束 */
  if (ota_upgrading) {
    printf("OTA: Waiting for upgrade to complete...\n");
    pthread_join(ota_thread, NULL);
  }
  printf("OTA manager cleaned up\n");
}

ota_error_t ota_start_upgrade(const ota_info_t *info) {
  if (!info) {
    return OTA_ERROR_INVALID;
  }

  if (ota_upgrading) {
    return OTA_ERROR_BUSY;
  }

  /* 检查必要参数 */
  if (strlen(info->url) == 0) {
    return OTA_ERROR_INVALID;
  }

  /* 复制升级信息 */
  ota_info_t *info_copy = (ota_info_t *)malloc(sizeof(ota_info_t));
  if (!info_copy) {
    return OTA_ERROR;
  }
  memcpy(info_copy, info, sizeof(ota_info_t));

  /* 启动升级线程 */
  ota_upgrading = 1;
  if (pthread_create(&ota_thread, NULL, ota_upgrade_thread, info_copy) != 0) {
    printf("OTA: Failed to create upgrade thread\n");
    free(info_copy);
    ota_upgrading = 0;
    return OTA_ERROR;
  }

  printf("OTA: Upgrade started for version %s\n", info->version);
  return OTA_OK;
}

ota_error_t ota_get_status(ota_status_t *status) {
  if (!status) {
    return OTA_ERROR_INVALID;
  }

  pthread_mutex_lock(&ota_mutex);
  memcpy(status, &ota_status, sizeof(ota_status_t));
  pthread_mutex_unlock(&ota_mutex);

  return OTA_OK;
}

ota_error_t ota_rollback(void) {
  if (ota_upgrading) {
    return OTA_ERROR_BUSY;
  }

  /* 检查备份是否存在 */
  if (access(OTA_BACKUP_PATH, F_OK) != 0) {
    printf("OTA: No backup firmware found\n");
    return OTA_ERROR_ROLLBACK;
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.state = OTA_STATE_ROLLBACK;
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  /* 恢复备份 */
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cp -f %s %s && chmod +x %s", OTA_BACKUP_PATH,
           OTA_CURRENT_PATH, OTA_CURRENT_PATH);

  int ret = system(cmd);
  if (ret != 0) {
    pthread_mutex_lock(&ota_mutex);
    ota_status.state = OTA_STATE_FAILED;
    strncpy(ota_status.error_msg, "Rollback failed", sizeof(ota_status.error_msg) - 1);
    save_ota_state();
    pthread_mutex_unlock(&ota_mutex);
    return OTA_ERROR_ROLLBACK;
  }

  pthread_mutex_lock(&ota_mutex);
  ota_status.state = OTA_STATE_IDLE;
  strncpy(ota_status.error_msg, "", sizeof(ota_status.error_msg));
  save_ota_state();
  pthread_mutex_unlock(&ota_mutex);

  printf("OTA: Rollback successful\n");

  /* 重启服务 */
  restart_service();

  return OTA_OK;
}

int ota_check_update(const char *current_version, char *latest_version,
                     int version_size) {
  /* 简单版本比较：通过MQTT查询云端最新版本 */
  /* 这里提供一个基础实现，实际需要与云端交互 */
  if (!current_version || !latest_version || version_size <= 0) {
    return -1;
  }

  /* 暂时返回无更新，实际实现需要MQTT查询 */
  strncpy(latest_version, current_version, version_size - 1);
  return 0;
}

ota_error_t ota_get_current_version(char *version, int size) {
  if (!version || size <= 0) {
    return OTA_ERROR_INVALID;
  }

  strncpy(version, FIRMWARE_VERSION, size - 1);
  return OTA_OK;
}

const char *ota_get_error_string(ota_error_t error) {
  switch (error) {
  case OTA_OK:
    return "Success";
  case OTA_ERROR:
    return "Generic error";
  case OTA_ERROR_DOWNLOAD:
    return "Download failed";
  case OTA_ERROR_CHECKSUM:
    return "Checksum verification failed";
  case OTA_ERROR_BACKUP:
    return "Backup failed";
  case OTA_ERROR_INSTALL:
    return "Installation failed";
  case OTA_ERROR_ROLLBACK:
    return "Rollback failed";
  case OTA_ERROR_INVALID:
    return "Invalid parameter";
  case OTA_ERROR_BUSY:
    return "Upgrade in progress";
  default:
    return "Unknown error";
  }
}

const char *ota_get_state_string(ota_state_t state) {
  switch (state) {
  case OTA_STATE_IDLE:
    return "Idle";
  case OTA_STATE_DOWNLOADING:
    return "Downloading";
  case OTA_STATE_VERIFYING:
    return "Verifying";
  case OTA_STATE_INSTALLING:
    return "Installing";
  case OTA_STATE_REBOOTING:
    return "Rebooting";
  case OTA_STATE_ROLLBACK:
    return "Rolling back";
  case OTA_STATE_SUCCESS:
    return "Success";
  case OTA_STATE_FAILED:
    return "Failed";
  default:
    return "Unknown";
  }
}
