/**
 * @file device_auth.c
 * @brief 设备认证模块实现
 * @author zhuxiangbo
 * @date 2026-05-30
 * @version 1.0
 *
 * 实现设备身份认证功能，包括设备ID生成、Token管理、TLS证书路径管理。
 */

#include "device_auth.h"
#include "cJSON.h"
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              内部函数 */
/* ========================================================================== */

/**
 * @brief 获取网卡MAC地址
 * @param mac_addr 输出MAC地址字符串（格式：XX:XX:XX:XX:XX:XX）
 * @param size 缓冲区大小
 * @return 0成功, -1失败
 */
static int get_mac_address(char *mac_addr, int size) {
  struct ifreq ifr;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return -1;
  }

  /* 尝试获取eth0的MAC地址 */
  strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
  if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
    /* eth0失败，尝试wlan0 */
    strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
      close(sock);
      return -1;
    }
  }
  close(sock);

  unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
  snprintf(mac_addr, size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);

  return 0;
}

/**
 * @brief 生成随机Token
 * @param token 输出Token缓冲区
 * @param size 缓冲区大小
 */
static void generate_random_token(char *token, int size) {
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  int len = size - 1;

  srand((unsigned int)(time(NULL) ^ getpid()));

  for (int i = 0; i < len; i++) {
    token[i] = charset[rand() % (sizeof(charset) - 1)];
  }
  token[len] = '\0';
}

/**
 * @brief 确保目录存在
 * @param dir_path 目录路径
 * @return 0成功, -1失败
 */
static int ensure_directory(const char *dir_path) {
  struct stat st;
  if (stat(dir_path, &st) == 0) {
    return 0;  /* 目录已存在 */
  }
  return mkdir(dir_path, 0700);
}

/* ========================================================================== */
/*                              公共接口实现 */
/* ========================================================================== */

devauth_error_t devauth_init(void) {
  /* 确保设备凭证目录存在 */
  ensure_directory("/etc/device");
  ensure_directory(TLS_CERT_DIR);
  return DEVAUTH_OK;
}

devauth_error_t devauth_get_device_id(char *device_id, int size) {
  if (device_id == NULL || size <= 0) {
    return DEVAUTH_ERROR;
  }

  /* 尝试从MAC地址生成设备ID */
  if (get_mac_address(device_id, size) == 0) {
    return DEVAUTH_OK;
  }

  /* MAC获取失败，使用主机名+随机数 */
  char hostname[64];
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    strncpy(hostname, "unknown", sizeof(hostname));
  }

  srand((unsigned int)(time(NULL) ^ getpid()));
  snprintf(device_id, size, "%s-%08X", hostname, (unsigned int)rand());

  return DEVAUTH_OK;
}

devauth_error_t devauth_load_credentials(devauth_info_t *info) {
  if (info == NULL) {
    return DEVAUTH_ERROR;
  }

  memset(info, 0, sizeof(devauth_info_t));

  /* 检查凭证文件是否存在 */
  if (access(DEVICE_CREDENTIAL_FILE, F_OK) != 0) {
    info->state = DEVAUTH_STATE_UNREGISTERED;
    return DEVAUTH_ERROR_NOT_FOUND;
  }

  /* 读取凭证文件 */
  FILE *fp = fopen(DEVICE_CREDENTIAL_FILE, "r");
  if (!fp) {
    return DEVAUTH_ERROR_FILE;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (file_size <= 0 || file_size > 4096) {
    fclose(fp);
    return DEVAUTH_ERROR_FILE;
  }

  char *json_str = (char *)malloc(file_size + 1);
  if (!json_str) {
    fclose(fp);
    return DEVAUTH_ERROR;
  }

  fread(json_str, 1, file_size, fp);
  json_str[file_size] = '\0';
  fclose(fp);

  /* 解析JSON */
  cJSON *root = cJSON_Parse(json_str);
  free(json_str);

  if (!root) {
    return DEVAUTH_ERROR_JSON;
  }

  /* 提取字段 */
  cJSON *id_obj = cJSON_GetObjectItem(root, "device_id");
  cJSON *token_obj = cJSON_GetObjectItem(root, "device_token");
  cJSON *ca_obj = cJSON_GetObjectItem(root, "ca_cert_path");
  cJSON *cert_obj = cJSON_GetObjectItem(root, "client_cert_path");
  cJSON *key_obj = cJSON_GetObjectItem(root, "client_key_path");
  cJSON *tls_obj = cJSON_GetObjectItem(root, "use_tls");

  if (id_obj && cJSON_IsString(id_obj)) {
    strncpy(info->device_id, id_obj->valuestring, DEVICE_ID_MAX_LEN - 1);
  }
  if (token_obj && cJSON_IsString(token_obj)) {
    strncpy(info->device_token, token_obj->valuestring,
            DEVICE_TOKEN_MAX_LEN - 1);
  }
  if (ca_obj && cJSON_IsString(ca_obj)) {
    strncpy(info->ca_cert_path, ca_obj->valuestring, CERT_PATH_MAX_LEN - 1);
  }
  if (cert_obj && cJSON_IsString(cert_obj)) {
    strncpy(info->client_cert_path, cert_obj->valuestring,
            CERT_PATH_MAX_LEN - 1);
  }
  if (key_obj && cJSON_IsString(key_obj)) {
    strncpy(info->client_key_path, key_obj->valuestring,
            CERT_PATH_MAX_LEN - 1);
  }
  if (tls_obj && cJSON_IsNumber(tls_obj)) {
    info->use_tls = tls_obj->valueint;
  }

  cJSON_Delete(root);

  /* 检查凭证完整性 */
  if (strlen(info->device_id) > 0 && strlen(info->device_token) > 0) {
    info->state = DEVAUTH_STATE_REGISTERED;
  } else {
    info->state = DEVAUTH_STATE_UNKNOWN;
  }

  return DEVAUTH_OK;
}

devauth_error_t devauth_save_credentials(const devauth_info_t *info) {
  if (info == NULL) {
    return DEVAUTH_ERROR;
  }

  /* 确保目录存在 */
  ensure_directory("/etc/device");

  /* 构建JSON */
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return DEVAUTH_ERROR;
  }

  cJSON_AddStringToObject(root, "device_id", info->device_id);
  cJSON_AddStringToObject(root, "device_token", info->device_token);
  cJSON_AddStringToObject(root, "ca_cert_path", info->ca_cert_path);
  cJSON_AddStringToObject(root, "client_cert_path", info->client_cert_path);
  cJSON_AddStringToObject(root, "client_key_path", info->client_key_path);
  cJSON_AddNumberToObject(root, "use_tls", info->use_tls);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!json_str) {
    return DEVAUTH_ERROR_JSON;
  }

  /* 写入文件 */
  FILE *fp = fopen(DEVICE_CREDENTIAL_FILE, "w");
  if (!fp) {
    free(json_str);
    return DEVAUTH_ERROR_FILE;
  }

  fwrite(json_str, 1, strlen(json_str), fp);
  fclose(fp);
  free(json_str);

  /* 设置文件权限（仅root可读写） */
  chmod(DEVICE_CREDENTIAL_FILE, 0600);

  return DEVAUTH_OK;
}

devauth_error_t devauth_register_device(devauth_info_t *info) {
  if (info == NULL) {
    return DEVAUTH_ERROR;
  }

  memset(info, 0, sizeof(devauth_info_t));

  /* 生成设备ID */
  devauth_error_t ret = devauth_get_device_id(info->device_id, DEVICE_ID_MAX_LEN);
  if (ret != DEVAUTH_OK) {
    return ret;
  }

  /* 生成随机Token */
  generate_random_token(info->device_token, DEVICE_TOKEN_MAX_LEN);

  /* 设置TLS状态 */
  info->use_tls = devauth_is_tls_enabled();

  /* 设置默认证书路径 */
  snprintf(info->ca_cert_path, CERT_PATH_MAX_LEN, "%s/ca.crt", TLS_CERT_DIR);
  snprintf(info->client_cert_path, CERT_PATH_MAX_LEN, "%s/client.crt",
           TLS_CERT_DIR);
  snprintf(info->client_key_path, CERT_PATH_MAX_LEN, "%s/client.key",
           TLS_CERT_DIR);

  info->state = DEVAUTH_STATE_REGISTERED;

  /* 保存凭证 */
  ret = devauth_save_credentials(info);
  if (ret != DEVAUTH_OK) {
    return ret;
  }

  printf("Device registered: ID=%s\n", info->device_id);
  return DEVAUTH_OK;
}

int devauth_is_registered(void) {
  return (access(DEVICE_CREDENTIAL_FILE, F_OK) == 0);
}

int devauth_is_tls_enabled(void) {
  const char *tls_env = getenv("MQTT_USE_TLS");
  if (tls_env && (strcmp(tls_env, "1") == 0 || strcmp(tls_env, "true") == 0 ||
                  strcmp(tls_env, "yes") == 0)) {
    return 1;
  }
  return 0;
}

const char *devauth_get_error_string(devauth_error_t error) {
  switch (error) {
  case DEVAUTH_OK:
    return "Success";
  case DEVAUTH_ERROR:
    return "Generic error";
  case DEVAUTH_ERROR_MAC:
    return "Failed to get MAC address";
  case DEVAUTH_ERROR_FILE:
    return "File operation failed";
  case DEVAUTH_ERROR_JSON:
    return "JSON parse error";
  case DEVAUTH_ERROR_NOT_FOUND:
    return "Credentials not found";
  case DEVAUTH_ERROR_INVALID:
    return "Invalid credentials";
  default:
    return "Unknown error";
  }
}
