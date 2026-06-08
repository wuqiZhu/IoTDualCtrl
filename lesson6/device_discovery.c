/**
 * @file device_discovery.c
 * @brief 设备发现模块实现
 * @author zhuxiangbo
 * @date 2026-05-31
 * @version 1.0
 *
 * 实现局域网设备发现功能，基于UDP广播。
 */

#include "device_discovery.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static device_info_t g_devices[DISCOVERY_MAX_DEVICES];
static int g_device_count = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static discovery_config_t g_config;
static bool g_initialized = false;
static bool g_running = false;
static int g_sockfd = -1;
static pthread_t g_listen_thread;
static pthread_t g_broadcast_thread;

static discovery_callback_t g_callback = NULL;
static void *g_callback_data = NULL;

static const char *g_status_strings[] = {"Offline", "Online", "Unknown"};
static const char *g_type_strings[] = {"Unknown", "Sensor", "Actuator",
                                       "Gateway", "Camera", "Display"};
static const char *g_error_strings[] = {
    "OK",           "General error",     "Invalid parameter",
    "Device not found", "Device table full", "Device already exists",
    "Socket error", "Bind error",        "Send error"};

static int find_device(const char *id) {
  for (int i = 0; i < g_device_count; i++) {
    if (strcmp(g_devices[i].id, id) == 0) return i;
  }
  return -1;
}

static void *listen_thread_func(void *arg) {
  (void)arg;
  char buf[512];
  struct sockaddr_in sender;
  socklen_t sender_len = sizeof(sender);

  while (g_running) {
    ssize_t n = recvfrom(g_sockfd, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&sender, &sender_len);
    if (n <= 0) continue;
    buf[n] = '\0';

    device_info_t info;
    memset(&info, 0, sizeof(info));

    char *id_str = strstr(buf, "ID:");
    char *name_str = strstr(buf, "NAME:");
    char *type_str = strstr(buf, "TYPE:");
    char *ver_str = strstr(buf, "VER:");

    if (id_str) {
      id_str += 3;
      char *end = strchr(id_str, ';');
      if (end) {
        size_t len = (size_t)(end - id_str);
        if (len >= sizeof(info.id)) len = sizeof(info.id) - 1;
        memcpy(info.id, id_str, len);
      }
    } else {
      continue;
    }

    strncpy(info.ip, inet_ntoa(sender.sin_addr), sizeof(info.ip) - 1);
    info.port = ntohs(sender.sin_port);
    info.status = DEVICE_STATUS_ONLINE;
    info.last_seen = time(NULL);

    if (name_str) {
      name_str += 5;
      char *end = strchr(name_str, ';');
      if (end) {
        size_t len = (size_t)(end - name_str);
        if (len >= sizeof(info.name)) len = sizeof(info.name) - 1;
        memcpy(info.name, name_str, len);
      }
    }

    if (type_str) {
      type_str += 5;
      info.type = (device_type_t)atoi(type_str);
    }

    if (ver_str) {
      ver_str += 4;
      char *end = strchr(ver_str, ';');
      if (end) {
        size_t len = (size_t)(end - ver_str);
        if (len >= sizeof(info.firmware_version)) len = sizeof(info.firmware_version) - 1;
        memcpy(info.firmware_version, ver_str, len);
      }
    }

    pthread_mutex_lock(&g_lock);
    int idx = find_device(info.id);
    if (idx >= 0) {
      strncpy(g_devices[idx].ip, info.ip, sizeof(g_devices[idx].ip) - 1);
      g_devices[idx].port = info.port;
      g_devices[idx].status = DEVICE_STATUS_ONLINE;
      g_devices[idx].last_seen = info.last_seen;
      if (strlen(info.name) > 0)
        strncpy(g_devices[idx].name, info.name, sizeof(g_devices[idx].name) - 1);
      if (strlen(info.firmware_version) > 0)
        strncpy(g_devices[idx].firmware_version, info.firmware_version,
                sizeof(g_devices[idx].firmware_version) - 1);
    } else if (g_device_count < DISCOVERY_MAX_DEVICES) {
      info.first_seen = info.last_seen;
      g_devices[g_device_count++] = info;
    }
    pthread_mutex_unlock(&g_lock);

    if (g_callback) {
      g_callback(&info, g_callback_data);
    }
  }
  return NULL;
}

static void *broadcast_thread_func(void *arg) {
  (void)arg;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(g_config.port);
  addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  while (g_running) {
    const char *msg = "DISCOVER;TYPE=gateway;";
    sendto(g_sockfd, msg, strlen(msg), 0,
           (struct sockaddr *)&addr, sizeof(addr));
    sleep(g_config.broadcast_interval);
  }
  return NULL;
}

discovery_error_t discovery_init(const discovery_config_t *config) {
  if (g_initialized) return DISCOVERY_OK;

  memset(g_devices, 0, sizeof(g_devices));
  g_device_count = 0;

  if (config) {
    g_config = *config;
  } else {
    g_config.port = DISCOVERY_PORT;
    g_config.broadcast_interval = DISCOVERY_BROADCAST_INTERVAL;
    g_config.timeout = DISCOVERY_TIMEOUT;
    g_config.enable_broadcast = true;
    g_config.enable_listen = true;
    g_config.group[0] = '\0';
  }

  g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (g_sockfd < 0) return DISCOVERY_ERROR_SOCKET;

  int opt = 1;
  setsockopt(g_sockfd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
  setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(g_config.port);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(g_sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    close(g_sockfd);
    g_sockfd = -1;
    return DISCOVERY_ERROR_BIND;
  }

  g_initialized = true;
  printf("[DISCOVERY] Initialized on port %d\n", g_config.port);
  return DISCOVERY_OK;
}

void discovery_cleanup(void) {
  if (!g_initialized) return;

  discovery_stop();

  if (g_sockfd >= 0) {
    close(g_sockfd);
    g_sockfd = -1;
  }

  pthread_mutex_lock(&g_lock);
  g_device_count = 0;
  pthread_mutex_unlock(&g_lock);

  g_initialized = false;
  printf("[DISCOVERY] Cleaned up\n");
}

discovery_error_t discovery_start(void) {
  if (!g_initialized) return DISCOVERY_ERROR;
  if (g_running) return DISCOVERY_OK;

  g_running = true;

  if (g_config.enable_listen) {
    pthread_create(&g_listen_thread, NULL, listen_thread_func, NULL);
  }

  if (g_config.enable_broadcast) {
    pthread_create(&g_broadcast_thread, NULL, broadcast_thread_func, NULL);
  }

  printf("[DISCOVERY] Started\n");
  return DISCOVERY_OK;
}

discovery_error_t discovery_stop(void) {
  if (!g_running) return DISCOVERY_OK;

  g_running = false;

  if (g_config.enable_listen) {
    pthread_join(g_listen_thread, NULL);
  }
  if (g_config.enable_broadcast) {
    pthread_join(g_broadcast_thread, NULL);
  }

  printf("[DISCOVERY] Stopped\n");
  return DISCOVERY_OK;
}

discovery_error_t discovery_broadcast(void) {
  if (!g_initialized) return DISCOVERY_ERROR;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(g_config.port);
  addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  const char *msg = "DISCOVER;TYPE=gateway;";
  if (sendto(g_sockfd, msg, strlen(msg), 0,
             (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    return DISCOVERY_ERROR_SEND;
  }
  return DISCOVERY_OK;
}

discovery_error_t discovery_register(const device_info_t *info) {
  if (!info || !g_initialized) return DISCOVERY_ERROR_PARAM;

  pthread_mutex_lock(&g_lock);

  if (find_device(info->id) >= 0) {
    pthread_mutex_unlock(&g_lock);
    return DISCOVERY_ERROR_ALREADY;
  }

  if (g_device_count >= DISCOVERY_MAX_DEVICES) {
    pthread_mutex_unlock(&g_lock);
    return DISCOVERY_ERROR_FULL;
  }

  device_info_t *dev = &g_devices[g_device_count++];
  *dev = *info;
  dev->first_seen = time(NULL);
  dev->last_seen = dev->first_seen;
  dev->status = DEVICE_STATUS_ONLINE;

  pthread_mutex_unlock(&g_lock);
  printf("[DISCOVERY] Device registered: %s (%s)\n", info->id, info->name);
  return DISCOVERY_OK;
}

discovery_error_t discovery_unregister(const char *id) {
  if (!id || !g_initialized) return DISCOVERY_ERROR_PARAM;

  pthread_mutex_lock(&g_lock);
  int idx = find_device(id);
  if (idx < 0) {
    pthread_mutex_unlock(&g_lock);
    return DISCOVERY_ERROR_NOT_FOUND;
  }

  for (int i = idx; i < g_device_count - 1; i++) {
    g_devices[i] = g_devices[i + 1];
  }
  g_device_count--;

  pthread_mutex_unlock(&g_lock);
  printf("[DISCOVERY] Device unregistered: %s\n", id);
  return DISCOVERY_OK;
}

discovery_error_t discovery_get_device(const char *id, device_info_t *info) {
  if (!id || !info || !g_initialized) return DISCOVERY_ERROR_PARAM;

  pthread_mutex_lock(&g_lock);
  int idx = find_device(id);
  if (idx < 0) {
    pthread_mutex_unlock(&g_lock);
    return DISCOVERY_ERROR_NOT_FOUND;
  }
  *info = g_devices[idx];
  pthread_mutex_unlock(&g_lock);
  return DISCOVERY_OK;
}

discovery_error_t discovery_get_devices(device_info_t *devices, int max_count,
                                        int *count) {
  if (!devices || !count || !g_initialized) return DISCOVERY_ERROR_PARAM;

  pthread_mutex_lock(&g_lock);
  *count = g_device_count < max_count ? g_device_count : max_count;
  memcpy(devices, g_devices, sizeof(device_info_t) * (size_t)*count);
  pthread_mutex_unlock(&g_lock);
  return DISCOVERY_OK;
}

int discovery_get_online_count(void) {
  if (!g_initialized) return 0;

  int count = 0;
  pthread_mutex_lock(&g_lock);
  for (int i = 0; i < g_device_count; i++) {
    if (g_devices[i].status == DEVICE_STATUS_ONLINE) count++;
  }
  pthread_mutex_unlock(&g_lock);
  return count;
}

int discovery_cleanup_timeout(void) {
  if (!g_initialized) return 0;

  long now = time(NULL);
  int removed = 0;

  pthread_mutex_lock(&g_lock);
  for (int i = g_device_count - 1; i >= 0; i--) {
    if (now - g_devices[i].last_seen > g_config.timeout) {
      printf("[DISCOVERY] Device timeout: %s\n", g_devices[i].id);
      for (int j = i; j < g_device_count - 1; j++) {
        g_devices[j] = g_devices[j + 1];
      }
      g_device_count--;
      removed++;
    }
  }
  pthread_mutex_unlock(&g_lock);
  return removed;
}

void discovery_set_callback(discovery_callback_t callback, void *user_data) {
  g_callback = callback;
  g_callback_data = user_data;
}

const char *discovery_get_status_string(device_status_t status) {
  if (status >= 0 && status <= DEVICE_STATUS_UNKNOWN)
    return g_status_strings[status];
  return "Invalid";
}

const char *discovery_get_type_string(device_type_t type) {
  if (type >= 0 && type <= DEVICE_TYPE_DISPLAY)
    return g_type_strings[type];
  return "Invalid";
}

const char *discovery_get_error_string(discovery_error_t error) {
  int idx = -error;
  if (idx >= 0 && idx < 9) return g_error_strings[idx];
  return "Unknown error";
}

void discovery_print_devices(void) {
  if (!g_initialized) return;

  pthread_mutex_lock(&g_lock);
  printf("\n=== Device List (%d devices) ===\n", g_device_count);
  printf("%-16s %-20s %-16s %-8s %-10s\n",
         "ID", "Name", "IP", "Type", "Status");
  printf("---------------- -------------------- ---------------- -------- ----------\n");

  for (int i = 0; i < g_device_count; i++) {
    printf("%-16s %-20s %-16s %-8s %-10s\n",
           g_devices[i].id,
           g_devices[i].name,
           g_devices[i].ip,
           discovery_get_type_string(g_devices[i].type),
           discovery_get_status_string(g_devices[i].status));
  }
  printf("=====================================\n\n");
  pthread_mutex_unlock(&g_lock);
}

bool discovery_is_running(void) {
  return g_running;
}
