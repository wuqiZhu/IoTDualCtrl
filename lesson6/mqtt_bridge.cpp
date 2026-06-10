/**
 * @file mqtt_bridge.cpp
 * @brief MQTT桥接服务 - 智能网关
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 2.0
 *
 * 本模块实现了MQTT与本地RPC服务的桥接，提供以下功能：
 * - 接收云端MQTT控制指令，转发到本地硬件
 * - 定时上报传感器遥测数据到云端
 * - 烟雾超标告警推送
 * - 智能自动控制（烟雾联动、温度联动、光照+PIR联动）
 *
 * MQTT主题：
 *   - device/control  : 接收控制指令
 *   - device/response : 发送执行结果
 *   - device/telemetry: 发送遥测数据
 *   - device/alert    : 发送告警信息
 */

#include "cJSON.h"
#include "mqttclient.h"
#include "rpc_client.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/stat.h>

/* 看门狗和数据缓存模块是C语言实现，需要extern "C"声明 */
extern "C" {
#include "watchdog.h"
#include "data_cache.h"
#include "config.h"
#include "system_monitor.h"
#include "device_auth.h"
#include "ota_manager.h"
#include "msg_queue.h"
#include "sensor_manager.h"
#include "security_audit.h"
#include "perf_monitor.h"
#include "plugin_manager.h"
#include "crypto_utils.h"
#include "memory_pool.h"
#include "device_discovery.h"
#include "log.h"
}

/* ========================================================================== */
/*                              本地Base64编码函数 */
/* ========================================================================== */

/** @brief Base64编码最大输出缓冲区（与云端64KB限制一致） */
#define LOCAL_BASE64_BUF_SIZE (64 * 1024)

/** @brief Base64编码字符表 */
static const char local_b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64编码（纯计算，无硬件依赖）
 * @param input 输入二进制数据
 * @param input_len 输入数据长度
 * @param output 输出Base64字符串缓冲区
 * @param output_size 输出缓冲区大小
 * @return 编码后的字符串长度, -1失败
 */
static int local_encode_base64(const unsigned char *input, int input_len,
                                char *output, int output_size) {
  int i, j;
  int output_len = 4 * ((input_len + 2) / 3);

  if (output_len >= output_size) {
    return -1;
  }

  for (i = 0, j = 0; i < input_len;) {
    unsigned int a = i < input_len ? input[i++] : 0;
    unsigned int b = i < input_len ? input[i++] : 0;
    unsigned int c = i < input_len ? input[i++] : 0;
    unsigned int t = (a << 16) | (b << 8) | c;
    output[j++] = local_b64_chars[(t >> 18) & 0x3F];
    output[j++] = local_b64_chars[(t >> 12) & 0x3F];
    output[j++] = local_b64_chars[(t >> 6) & 0x3F];
    output[j++] = local_b64_chars[t & 0x3F];
  }

  for (i = 0; i < (3 - (input_len % 3)) % 3; i++) {
    output[output_len - 1 - i] = '=';
  }

  output[output_len] = '\0';
  return output_len;
}

/* ========================================================================== */
/*                              MQTT主题定义 */
/* ========================================================================== */

/** @brief 控制指令主题 */
#define CMD_TOPIC "device/control"

/** @brief 响应结果主题 */
#define RSP_TOPIC "device/response"

/** @brief 遥测数据主题 */
#define TELEMETRY_TOPIC "device/telemetry"

/** @brief 告警信息主题 */
#define ALERT_TOPIC "device/alert"

/** @brief 心跳上报主题 */
#define HEARTBEAT_TOPIC "device/heartbeat"

/* ========================================================================== */
/*                              系统参数定义 */
/* ========================================================================== */

/** @brief 遥测数据上报间隔（秒） */
#define TELEMETRY_INTERVAL 5

/** @brief 心跳上报间隔（秒） */
#define HEARTBEAT_INTERVAL 60

/** @brief PIR无人延时关闭时间（秒） */
#define PIR_OFF_DELAY 30

/** @brief MQTT重连最大尝试次数 */
#define MQTT_RECONNECT_MAX_RETRIES 5

/** @brief MQTT重连间隔（秒） */
#define MQTT_RECONNECT_DELAY 5

/** @brief 温度变化上报阈值（摄氏度） */
#define TEMP_CHANGE_THRESHOLD 1

/** @brief 湿度变化上报阈值（百分比） */
#define HUMI_CHANGE_THRESHOLD 5

/** @brief 全量上报间隔（秒）- 即使无变化也定期上报 */
#define FULL_REPORT_INTERVAL 300

/* ========================================================================== */
/*                              全局变量 */
/* ========================================================================== */

/** @brief MQTT客户端实例 */
static mqtt_client_t *client = NULL;

/** @brief 程序运行标志 (0=停止) */
static int running = 1;

/** @brief 全局配置（由config_load_combined填充） */
static app_config_t app_config;

/** @brief MQTT连接状态 (0=断开, 1=已连接) */
static int mqtt_connected = 0;

/** @brief 烟雾报警电平 (从配置读取) */
static int smoke_alert_level = 0;

/** @brief 烟雾报警后风扇运行时间（秒） */
static int smoke_fan_duration = 30;

/** @brief 烟雾告警发送间隔（秒） */
static int smoke_alert_interval = 10;

/** @brief 温度上限阈值（从配置读取） */
static int temp_high_threshold = 32;

/** @brief 温度下限阈值（从配置读取） */
static int temp_low_threshold = 30;

/** @brief 烟雾传感器故障检测阈值（秒） */
static int smoke_fault_timeout = 60;

/** @brief 烟雾持续报警起始时间 */
static time_t smoke_alarm_start_time = 0;

/** @brief 烟雾传感器故障标志 (0=正常, 1=故障) */
static int smoke_sensor_fault = 0;

/** @brief 上次故障告警时间 */
static time_t last_fault_alert_time = 0;

/** @brief 风扇状态 (0=关闭, 1=开启) */
static int fan_state = 0;

/** @brief LED灯状态 (0=关闭, 1=开启) */
static int led_state = 0;

/** @brief PIR无人计时起点 */
static time_t last_pir_off_time = 0;

/** @brief 命令消息队列（解耦MQTT回调和RPC执行） */
static msg_queue_t *cmd_queue = NULL;

/** @brief 命令工作线程运行标志 */
static int cmd_worker_running = 1;

/** @brief 缓存的传感器数据（每轮循环只读取一次） */
typedef struct {
  int pir;
  int light;
  int humi;
  int temp;
  int relay;
  int relay2;
  int smoke_digital;
  int valid;
} cached_sensor_data_t;

static cached_sensor_data_t g_sensor_cache = {-1, -1, -1, -1, -1, -1, -1, 0};

/* ========================================================================== */
/*                              事件驱动 - 上次上报值 */
/* ========================================================================== */

/** @brief 上次上报的PIR值（-1表示未上报过） */
static int last_reported_pir = -1;

/** @brief 上次上报的光照值 */
static int last_reported_light = -1;

/** @brief 上次上报的温度值 */
static int last_reported_temp = -999;

/** @brief 上次上报的湿度值 */
static int last_reported_humi = -999;

/** @brief 上次上报的继电器1状态 */
static int last_reported_relay = -1;

/** @brief 上次上报的继电器2状态 */
static int last_reported_relay2 = -1;

/** @brief 上次上报的烟雾数字值 */
static int last_reported_smoke = -1;

/** @brief 上次全量上报时间 */
static time_t last_full_report_time = 0;

/* ========================================================================== */
/*                              MQTT配置变量 */
/* ========================================================================== */

/** @brief MQTT服务器地址（从环境变量读取） */
static const char *mqtt_host = NULL;

/** @brief MQTT服务器端口（从环境变量读取，默认1883） */
static const char *mqtt_port = NULL;

/** @brief MQTT用户名（从环境变量读取） */
static const char *mqtt_username = NULL;

/** @brief MQTT密码（从环境变量读取） */
static const char *mqtt_password = NULL;

/** @brief MQTT客户端ID（从环境变量读取，默认mqtt_bridge） */
static const char *mqtt_clientid = NULL;

/* ========================================================================== */
/*                              配置加载函数 */
/* ========================================================================== */

/**
 * @brief 从环境变量加载MQTT配置
 * @return 0成功, -1失败（缺少必要环境变量）
 *
 * 必要环境变量：
 *   - MQTT_HOST: MQTT服务器地址
 *   - MQTT_USERNAME: MQTT用户名
 *   - MQTT_PASSWORD: MQTT密码
 *
 * 可选环境变量：
 *   - MQTT_PORT: MQTT端口（默认1883）
 *   - MQTT_CLIENTID: 客户端ID（默认mqtt_bridge）
 */
static int load_mqtt_config(void) {
  mqtt_host = getenv("MQTT_HOST");
  mqtt_port = getenv("MQTT_PORT");
  mqtt_username = getenv("MQTT_USERNAME");
  mqtt_password = getenv("MQTT_PASSWORD");
  mqtt_clientid = getenv("MQTT_CLIENTID");

  if (!mqtt_host) {
    fprintf(stderr, "Missing environment variable: MQTT_HOST\n");
    return -1;
  }
  if (!mqtt_username) {
    fprintf(stderr, "Missing environment variable: MQTT_USERNAME\n");
    return -1;
  }
  if (!mqtt_password) {
    fprintf(stderr, "Missing environment variable: MQTT_PASSWORD\n");
    return -1;
  }

  /* 设置默认值 */
  if (!mqtt_port) {
    mqtt_port = "1883";
  }
  if (!mqtt_clientid) {
    mqtt_clientid = "mqtt_bridge";
  }

  return 0;
}

/* ========================================================================== */
/*                              MQTT发布函数 */
/* ========================================================================== */

/**
 * @brief 发布RPC响应到MQTT
 * @param method 方法名
 * @param success 是否成功 (1=成功, 0=失败)
 * @param extra_json 额外数据JSON字符串（可选）
 */
static void publish_response(const char *method, int success,
                             const char *extra_json) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "method", method);
  cJSON_AddNumberToObject(root, "success", success);

  if (extra_json) {
    cJSON *extra = cJSON_Parse(extra_json);
    if (extra) {
      cJSON_AddItemToObject(root, "data", extra);
    } else {
      cJSON_AddStringToObject(root, "data", extra_json);
    }
  }

  char *payload = cJSON_PrintUnformatted(root);
  if (payload) {
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.payload = (void *)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = QOS1;
    mqtt_publish(client, RSP_TOPIC, &msg);
    free(payload);
  }
  cJSON_Delete(root);
}

/**
 * @brief 发布告警信息到MQTT
 * @param type 告警类型
 * @param level 告警级别
 */
static void publish_alert(const char *type, int level) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "method", "smoke_alert");
  cJSON_AddStringToObject(root, "type", type);
  cJSON_AddNumberToObject(root, "level", level);
  cJSON_AddNumberToObject(root, "timestamp", time(NULL));

  char *payload = cJSON_PrintUnformatted(root);
  if (payload) {
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.payload = (void *)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = QOS1;
    mqtt_publish(client, ALERT_TOPIC, &msg);
    printf("Alert published: %s\n", payload);
    free(payload);
  }
  cJSON_Delete(root);
}

/**
 * @brief 错误级别定义
 */
typedef enum {
  ERROR_LEVEL_LOW = 0,      /**< 低优先级错误 */
  ERROR_LEVEL_MEDIUM = 1,   /**< 中优先级错误 */
  ERROR_LEVEL_HIGH = 2,     /**< 高优先级错误 */
  ERROR_LEVEL_CRITICAL = 3  /**< 严重错误 */
} error_level_t;

/**
 * @brief 发布错误报告到MQTT
 * @param level 错误级别
 * @param module 模块名称
 * @param func 函数名称
 * @param error_code 错误码
 * @param message 错误消息
 *
 * 统一错误上报接口，所有模块通过此函数发送错误信息到device/alert主题
 */
static void __attribute__((unused)) publish_error_report(error_level_t level, const char *module,
                                 const char *func, int error_code,
                                 const char *message) {
  if (!client || !mqtt_connected) {
    printf("MQTT not connected, error report skipped: [%s.%s] %s\n",
           module ? module : "unknown", func ? func : "unknown",
           message ? message : "no message");
    return;
  }

  /* 构建错误报告JSON */
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "method", "error_report");
  cJSON_AddNumberToObject(root, "level", (int)level);
  cJSON_AddStringToObject(root, "module", module ? module : "unknown");
  cJSON_AddStringToObject(root, "function", func ? func : "unknown");
  cJSON_AddNumberToObject(root, "error_code", error_code);
  cJSON_AddStringToObject(root, "message", message ? message : "no message");
  cJSON_AddNumberToObject(root, "timestamp", time(NULL));

  /* 添加设备ID */
  char device_id_buf[64];
  if (devauth_get_device_id(device_id_buf, sizeof(device_id_buf)) == DEVAUTH_OK) {
    cJSON_AddStringToObject(root, "device_id", device_id_buf);
  }

  char *payload = cJSON_PrintUnformatted(root);
  if (payload) {
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.payload = (void *)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = QOS1;
    mqtt_publish(client, ALERT_TOPIC, &msg);

    /* 同时记录到本地日志 */
    const char *level_str[] = {"LOW", "MEDIUM", "HIGH", "CRITICAL"};
    printf("[ERROR_REPORT][%s] %s.%s (code=%d): %s\n",
           level < 4 ? level_str[level] : "UNKNOWN",
           module ? module : "unknown", func ? func : "unknown",
           error_code, message ? message : "no message");

    free(payload);
  }
  cJSON_Delete(root);
}

/**
 * @brief 通过HTTP POST上传JPEG图片到云端服务器
 * @param host 服务器IP
 * @param port 端口
 * @param path URL路径
 * @param event_type 事件类型（放在X-Event-Type头中）
 * @param jpeg_data JPEG二进制数据
 * @param jpeg_len 数据长度
 * @return 0成功, -1失败
 *
 * 使用BSD socket直连，不依赖外部HTTP客户端库。
 * 5秒超时，防止阻塞主循环。
 */
static int http_post_jpeg(const char *host, int port, const char *path,
                           const char *event_type,
                           const unsigned char *jpeg_data, int jpeg_len)
{
  struct sockaddr_in addr;
  int sock;
  char request_buf[512];
  char response_buf[256];
  int ret, total_read;
  struct timeval tv;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    printf("HTTP upload: socket() failed\n");
    return -1;
  }

  /* 设置5秒超时 */
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
    printf("HTTP upload: invalid IP %s\n", host);
    close(sock);
    return -1;
  }

  ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    printf("HTTP upload: connect() failed to %s:%d\n", host, port);
    close(sock);
    return -1;
  }

  /* 构建HTTP POST请求 */
  snprintf(request_buf, sizeof(request_buf),
    "POST %s HTTP/1.1\r\n"
    "Host: %s:%d\r\n"
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %d\r\n"
    "X-Event-Type: %s\r\n"
    "Connection: close\r\n"
    "\r\n",
    path, host, port, jpeg_len, event_type ? event_type : "unknown");

  /* 发送请求头 */
  ret = send(sock, request_buf, strlen(request_buf), 0);
  if (ret < 0) {
    printf("HTTP upload: send headers failed\n");
    close(sock);
    return -1;
  }

  /* 发送JPEG数据 */
  ret = send(sock, jpeg_data, jpeg_len, 0);
  if (ret < 0) {
    printf("HTTP upload: send data failed\n");
    close(sock);
    return -1;
  }

  /* 读取响应 */
  total_read = 0;
  while (total_read < (int)sizeof(response_buf) - 1) {
    ret = recv(sock, response_buf + total_read,
               sizeof(response_buf) - 1 - total_read, 0);
    if (ret <= 0) break;
    total_read += ret;
  }
  response_buf[total_read] = '\0';

  close(sock);

  if (strstr(response_buf, "200 OK") ||
    strstr(response_buf, "\"status\":\"ok\"")) {
    return 0;
  }

  printf("HTTP upload: unexpected response: %s\n", response_buf);
  return -1;
}

/* 前向声明：publish_image_http的fallback会调用此函数 */
static void publish_image(const char *event_type);

/**
 * @brief 通过HTTP上传图片+MQTT小通知（替代原MQTT传base64方案）
 * @param event_type 事件类型
 *
 * 流程：
 * 1. 拍照并保存到临时文件
 * 2. HTTP POST上传JPEG到云端
 * 3. 成功 → 发一条小MQTT通知（不带图片数据）
 * 4. 失败 → 回退到原base64+MQTT方案
 */
static void publish_image_http(const char *event_type) {
  char jpeg_path[64];
  FILE *fp;
  long jpeg_len;
  unsigned char *jpeg_data = NULL;
  struct stat st;

  snprintf(jpeg_path, sizeof(jpeg_path), "/tmp/iot_http_%s.jpg",
           event_type ? event_type : "unknown");

  /* 1. 通过RPC拍照（避免直接操作摄像头硬件） */
  if (rpc_camera_capture_jpeg(jpeg_path) != 0) {
    printf("HTTP image: RPC camera capture failed\n");
    return;
  }

  /* 2. 读文件 */
  if (stat(jpeg_path, &st) != 0 || st.st_size <= 0) {
    printf("HTTP image: file stat failed\n");
    return;
  }
  jpeg_len = st.st_size;

  jpeg_data = (unsigned char *)malloc(jpeg_len);
  if (!jpeg_data) {
    printf("HTTP image: malloc failed\n");
    return;
  }

  fp = fopen(jpeg_path, "rb");
  if (!fp) {
    printf("HTTP image: fopen failed\n");
    free(jpeg_data);
    return;
  }
  fread(jpeg_data, 1, jpeg_len, fp);
  fclose(fp);
  unlink(jpeg_path);  /* 删除临时文件 */

  /* 3. HTTP上传 */
  int ret = http_post_jpeg(mqtt_host, 9090, "/upload",
                            event_type, jpeg_data, (int)jpeg_len);
  if (ret == 0) {
    printf("Image uploaded via HTTP: %s (%ld bytes)\n",
           event_type, jpeg_len);
    /* 成功：发一条小MQTT通知（仅事件信息，无图片数据） */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "image_uploaded");
    cJSON_AddStringToObject(root, "event", event_type);
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
      mqtt_message_t msg;
      memset(&msg, 0, sizeof(msg));
      msg.payload = (void *)payload;
      msg.payloadlen = strlen(payload);
      msg.qos = QOS1;
      (void)mqtt_publish(client, ALERT_TOPIC, &msg);
      free(payload);
    }
    cJSON_Delete(root);
  } else {
    printf("HTTP upload failed, fallback to MQTT base64...\n");
    free(jpeg_data);
    publish_image(event_type);
    return;
  }

  free(jpeg_data);
}

static void publish_image(const char *event_type) {
  char jpeg_path[64];
  unsigned char *jpeg_data = NULL;
  long jpeg_len;
  FILE *fp;
  struct stat st;

  snprintf(jpeg_path, sizeof(jpeg_path), "/tmp/rpc_capture_%s.jpg",
           event_type ? event_type : "unknown");

  /* 1. 通过RPC调用 rpc_server 拍照（避免直接操作摄像头硬件） */
  if (rpc_camera_capture_jpeg(jpeg_path) != 0) {
    printf("Image: RPC camera capture failed\n");
    return;
  }

  /* 2. 读取JPEG文件 */
  if (stat(jpeg_path, &st) != 0 || st.st_size <= 0) {
    printf("Image: file stat failed\n");
    return;
  }
  jpeg_len = st.st_size;

  jpeg_data = (unsigned char *)malloc(jpeg_len);
  if (!jpeg_data) {
    printf("Image: malloc failed\n");
    return;
  }

  fp = fopen(jpeg_path, "rb");
  if (!fp) {
    printf("Image: fopen failed\n");
    free(jpeg_data);
    return;
  }
  fread(jpeg_data, 1, jpeg_len, fp);
  fclose(fp);
  unlink(jpeg_path); /* 删除临时文件 */

  /* 3. 本地Base64编码 */
  static char base64_buf[LOCAL_BASE64_BUF_SIZE];
  int base64_len = local_encode_base64(jpeg_data, (int)jpeg_len,
                                        base64_buf, sizeof(base64_buf));
  free(jpeg_data);

  if (base64_len <= 0) {
    printf("Image: base64 encode failed\n");
    return;
  }

  /* 4. MQTT发布 */
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "method", "image_upload");
  cJSON_AddStringToObject(root, "event", event_type);
  cJSON_AddNumberToObject(root, "timestamp", time(NULL));
  cJSON_AddStringToObject(root, "image_data", base64_buf);
  cJSON_AddNumberToObject(root, "image_size", base64_len);

  char *payload = cJSON_PrintUnformatted(root);
  if (payload) {
    if (strlen(payload) <= LOCAL_BASE64_BUF_SIZE) {
      mqtt_message_t msg;
      memset(&msg, 0, sizeof(msg));
      msg.payload = (void *)payload;
      msg.payloadlen = strlen(payload);
      msg.qos = QOS1;
      mqtt_publish(client, ALERT_TOPIC, &msg);
      printf("Image uploaded: %s (%d bytes base64)\n", event_type, base64_len);
    } else {
      printf("Image too large for MQTT, skipping upload\n");
    }
    free(payload);
  }
  cJSON_Delete(root);
}

/**
 * @brief 发布心跳数据到MQTT
 *
 * 上报设备运行状态，包括：
 * - 系统指标（CPU、内存、负载、运行时间）
 * - 传感器状态摘要
 */
static void publish_heartbeat(void) {
  if (!client || !mqtt_connected) {
    return;
  }

  /* 获取系统状态 */
  sysmon_status_t sys_status;
  if (sysmon_get_status(&sys_status) != SYSMON_OK) {
    printf("Failed to get system status\n");
    return;
  }

  /* 构建心跳JSON */
  char sys_buf[512];
  if (sysmon_status_to_json(&sys_status, sys_buf, sizeof(sys_buf)) != SYSMON_OK) {
    printf("Failed to format system status\n");
    return;
  }

  /* 构建完整心跳消息 */
  cJSON *root = cJSON_Parse(sys_buf);
  if (!root) {
    return;
  }

  /* 添加设备信息 */
  cJSON_AddStringToObject(root, "device_id", mqtt_clientid);
  cJSON_AddStringToObject(root, "status", "online");

  char *payload = cJSON_PrintUnformatted(root);
  if (payload) {
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.payload = (void *)payload;
    msg.payloadlen = strlen(payload);
    msg.qos = QOS0;
    mqtt_publish(client, HEARTBEAT_TOPIC, &msg);
    printf("Heartbeat published: %s\n", payload);
    free(payload);
  }
  cJSON_Delete(root);
}

/**
 * @brief 读取所有传感器数据并缓存
 *
 * 每轮遥测循环只调用一次，缓存结果供publish_telemetry和auto_control共用，
 * 消除重复的RPC调用。
 */
static void refresh_sensor_cache(void) {
  char raw_humi, raw_temp;

  if (rpc_pir_read(&g_sensor_cache.pir) != 0)
    g_sensor_cache.pir = -1;
  if (rpc_light_read(&g_sensor_cache.light) != 0)
    g_sensor_cache.light = -1;
  if (rpc_dht11_read(&raw_humi, &raw_temp) == 0) {
    g_sensor_cache.humi = (int)raw_humi;
    g_sensor_cache.temp = (int)raw_temp;
  } else {
    g_sensor_cache.humi = -1;
    g_sensor_cache.temp = -1;
  }
  if (rpc_relay_read(&g_sensor_cache.relay) != 0)
    g_sensor_cache.relay = -1;
  if (rpc_relay2_read(&g_sensor_cache.relay2) != 0)
    g_sensor_cache.relay2 = -1;
  if (rpc_smoke_digital_read(&g_sensor_cache.smoke_digital) != 0)
    g_sensor_cache.smoke_digital = -1;

  g_sensor_cache.valid = 1;
}

/**
 * @brief 发布遥测数据到MQTT
 *
 * 实现事件驱动上报：
 * 1. 首次读取数据时立即上报
 * 2. 传感器数据发生显著变化时上报
 * 3. 定期全量上报（每5分钟）
 * 4. 自动控制逻辑始终执行
 */
static void publish_telemetry(void) {
  int pir, light, relay, relay2, smoke_digital;
  int humi = -1, temp = -1;
  int need_report = 0;

  /* 使用缓存的传感器数据（由refresh_sensor_cache填充） */
  if (!g_sensor_cache.valid) {
    return;
  }
  pir = g_sensor_cache.pir;
  light = g_sensor_cache.light;
  humi = g_sensor_cache.humi;
  temp = g_sensor_cache.temp;
  relay = g_sensor_cache.relay;
  relay2 = g_sensor_cache.relay2;
  smoke_digital = g_sensor_cache.smoke_digital;

  /* 检查是否需要上报 */
  time_t now = time(NULL);

  /* 首次上报（未上报过） */
  if (last_reported_pir == -1 && last_reported_temp == -999) {
    need_report = 1;
    printf("Event: First report\n");
  }

  /* PIR状态变化 */
  if (pir != -1 && pir != last_reported_pir && last_reported_pir != -1) {
    need_report = 1;
    printf("Event: PIR changed %d -> %d\n", last_reported_pir, pir);
  }

  /* 光照状态变化 */
  if (light != -1 && light != last_reported_light && last_reported_light != -1) {
    need_report = 1;
    printf("Event: Light changed %d -> %d\n", last_reported_light, light);
  }

  /* 温度变化超过阈值 */
  if (temp != -1 && last_reported_temp != -999 &&
      abs(temp - last_reported_temp) >= TEMP_CHANGE_THRESHOLD) {
    need_report = 1;
    printf("Event: Temp changed %d -> %d\n", last_reported_temp, temp);
  }

  /* 湿度变化超过阈值 */
  if (humi != -1 && last_reported_humi != -999 &&
      abs(humi - last_reported_humi) >= HUMI_CHANGE_THRESHOLD) {
    need_report = 1;
    printf("Event: Humi changed %d -> %d\n", last_reported_humi, humi);
  }

  /* 继电器状态变化 */
  if (relay != -1 && relay != last_reported_relay && last_reported_relay != -1) {
    need_report = 1;
    printf("Event: Relay1 changed %d -> %d\n", last_reported_relay, relay);
  }
  if (relay2 != -1 && relay2 != last_reported_relay2 && last_reported_relay2 != -1) {
    need_report = 1;
    printf("Event: Relay2 changed %d -> %d\n", last_reported_relay2, relay2);
  }

  /* 烟雾状态变化（烟雾检测是关键安全事件，必须立即上报） */
  if (smoke_digital != -1 && smoke_digital != last_reported_smoke &&
      last_reported_smoke != -1) {
    need_report = 1;
    printf("Event: Smoke changed %d -> %d\n", last_reported_smoke, smoke_digital);
  }

  /* 定期全量上报 */
  if (now - last_full_report_time >= FULL_REPORT_INTERVAL) {
    need_report = 1;
    printf("Event: Full report interval reached\n");
  }

  /* 执行上报 */
  if (need_report) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "telemetry");
    cJSON_AddNumberToObject(root, "success", 1);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "pir", pir);
    cJSON_AddNumberToObject(data, "light", light);
    cJSON_AddNumberToObject(data, "humi", humi);
    cJSON_AddNumberToObject(data, "temp", temp);
    cJSON_AddNumberToObject(data, "relay", relay);
    cJSON_AddNumberToObject(data, "relay2", relay2);
    cJSON_AddNumberToObject(data, "smoke_digital", smoke_digital);
    cJSON_AddItemToObject(root, "data", data);

    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
      /* 先尝试发送缓存中的数据 */
      while (!data_cache_is_empty() && mqtt_connected) {
        char cached_data[CACHE_ENTRY_MAX_LEN];
        int cached_len;
        if (data_cache_pop(cached_data, sizeof(cached_data), &cached_len) ==
            CACHE_OK) {
          mqtt_message_t cached_msg;
          memset(&cached_msg, 0, sizeof(cached_msg));
          cached_msg.payload = (void *)cached_data;
          cached_msg.payloadlen = cached_len;
          cached_msg.qos = QOS1;
          if (mqtt_publish(client, TELEMETRY_TOPIC, &cached_msg) == 0) {
            printf("Cached telemetry sent: %s\n", cached_data);
          } else {
            /* 发送失败，重新缓存 */
            data_cache_push(cached_data, cached_len);
            break;
          }
        }
      }

      /* 发送当前数据 */
      if (mqtt_connected) {
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.payload = (void *)payload;
        msg.payloadlen = strlen(payload);
        msg.qos = QOS1;
        if (mqtt_publish(client, TELEMETRY_TOPIC, &msg) == 0) {
          printf("Telemetry published: %s\n", payload);
        } else {
          /* 发送失败，缓存数据 */
          printf("MQTT publish failed, caching data\n");
          data_cache_push(payload, strlen(payload));
        }
      } else {
        /* MQTT未连接，缓存数据 */
        printf("MQTT disconnected, caching data\n");
        data_cache_push(payload, strlen(payload));
      }
      free(payload);
    }
    cJSON_Delete(root);

    /* 更新上次上报值 */
    last_reported_pir = pir;
    last_reported_light = light;
    last_reported_temp = (int)temp;
    last_reported_humi = (int)humi;
    last_reported_relay = relay;
    last_reported_relay2 = relay2;
    last_reported_smoke = smoke_digital;
    last_full_report_time = now;
  } else {
    printf("Telemetry skipped (no significant change)\n");
  }
}

/* ========================================================================== */
/*                              自动控制函数 */
/* ========================================================================== */

/**
 * @brief 烟雾联动控制
 * @param smoke_digital 烟雾传感器值 (0=检测到烟雾, 1=正常)
 * @param smoke_fan_until 风扇运行截止时间
 * @param last_alert_time 上次告警时间
 *
 * 当检测到烟雾时：
 * 1. 立即开启风扇
 * 2. 设置风扇运行 smoke_fan_duration 秒
 * 3. 每 smoke_alert_interval 秒发送一次告警
 * 4. 烟雾恢复正常后关闭风扇
 * 5. 烟雾持续报警超过 smoke_fault_timeout 秒时判定为传感器故障
 */
static void auto_control_smoke(int smoke_digital, time_t *smoke_fan_until,
                               time_t *last_alert_time) {
  time_t now = time(NULL);

  if (smoke_digital == -1) {
    return;
  }

  if (smoke_digital == smoke_alert_level) {
    /* 记录烟雾报警起始时间 */
    if (smoke_alarm_start_time == 0) {
      smoke_alarm_start_time = now;
    }

    /* 故障检测：持续报警超过阈值 */
    if (now - smoke_alarm_start_time >= smoke_fault_timeout) {
      if (!smoke_sensor_fault) {
        smoke_sensor_fault = 1;
        printf("WARNING: Smoke sensor fault detected! Continuous alarm for %d seconds\n",
               smoke_fault_timeout);
        publish_alert("smoke_sensor_fault", 1);
        last_fault_alert_time = now;
      }
      /* 故障状态下周期性告警，但不持续触发风扇 */
      if (now - last_fault_alert_time > smoke_alert_interval) {
        publish_alert("smoke_sensor_fault", 1);
        last_fault_alert_time = now;
      }
      return;
    }

    /* 检测到烟雾，开启风扇 */
    if (!fan_state) {
      if (rpc_relay_control(1) == 0) {
        fan_state = 1;
        printf("Auto: Fan ON due to smoke alert\n");
      }
    }

    /* 设置风扇运行时间 */
    *smoke_fan_until = now + smoke_fan_duration;

    /* 烟雾报警时自动拍照并上传 */
    if (now - *last_alert_time > smoke_alert_interval) {
      char photo_path[64];
      snprintf(photo_path, sizeof(photo_path), "/tmp/smoke_alert_%ld.jpg", now);
      if (rpc_camera_capture_jpeg(photo_path) == 0) {
        printf("Smoke alert photo saved: %s\n", photo_path);
      }
      publish_alert("smoke", 1);
      publish_image_http("smoke_alert");
      *last_alert_time = now;
    }
  } else {
    /* 烟雾恢复正常 */
    smoke_alarm_start_time = 0;
    smoke_sensor_fault = 0;

    /* 风扇运行时间到期处理
     * 注意：
     * 1. smoke_fan_until > 0 确保只在实际发生过烟雾告警后才控制
     *    避免与auto_control_temp的温度联动打架（初始值0导致now>=0总为真）
     * 2. 检查当前温度——如果温度仍超过高温阈值，不关风扇，
     *    交给 auto_control_temp 统一管理，避免继电器跳匝
     * 3. 处理完后重置 smoke_fan_until = 0，防止下次循环重复触发 */
    if (fan_state && *smoke_fan_until > 0 && now >= *smoke_fan_until) {
      /* 温度仍然偏高时，不介入，让温度联动自行管理 */
      if (g_sensor_cache.temp < temp_high_threshold ||
          g_sensor_cache.temp == -1) {
        if (rpc_relay_control(0) == 0) {
          fan_state = 0;
          printf("Auto: Fan OFF, smoke cleared (temp=%d°C)\n",
                 g_sensor_cache.temp);
        }
      } else {
        printf("Auto: Smoke cleared but temp=%d°C > %d, keeping fan ON\n",
               g_sensor_cache.temp, temp_high_threshold);
      }
      *smoke_fan_until = 0;  /* 重置，避免下次循环重复触发 */
    }
  }
}

/**
 * @brief 温度联动控制
 * @param temp 当前温度
 * @param smoke_digital 烟雾传感器值
 * @param smoke_fan_until 烟雾联动风扇截止时间
 *
 * 温度控制逻辑（带滞后）：
 * - 温度 > 32°C 且无烟雾报警 → 开启风扇
 * - 温度 < 30°C 且无烟雾报警 → 关闭风扇
 * - 烟雾报警期间不执行温度控制
 */
static void auto_control_temp(int temp, int smoke_digital,
                              time_t smoke_fan_until) {
  if (temp == -1) {
    return;
  }

  time_t now = time(NULL);

  /* 烟雾报警期间不执行温度控制 */
  if (smoke_digital == smoke_alert_level || now < smoke_fan_until) {
    return;
  }

  /* 温度控制逻辑 */
  if (temp > temp_high_threshold && !fan_state) {
    if (rpc_relay_control(1) == 0) {
      fan_state = 1;
      printf("Auto: Fan ON (temp=%d°C > %d)\n", temp, temp_high_threshold);
    }
  } else if (temp < temp_low_threshold && fan_state) {
    if (rpc_relay_control(0) == 0) {
      fan_state = 0;
      printf("Auto: Fan OFF (temp=%d°C < %d)\n", temp, temp_low_threshold);
    }
  }
}

/**
 * @brief 光照+PIR联动控制
 * @param light 光照值 (0=明亮, 1=黑暗)
 * @param pir PIR值 (0=无人, 1=有人)
 *
 * 控制逻辑：
 * - 天黑且有人 → 开启LED
 * - 无人超过30秒 → 关闭LED
 */
static void auto_control_light_pir(int light, int pir) {
  if (light == -1 || pir == -1) {
    return;
  }

  time_t now = time(NULL);

  if (light == 1 && pir == 1) {
    /* 天黑且有人，开启LED */
    if (!led_state) {
      if (rpc_relay2_control(1) == 0) {
        led_state = 1;
        printf("Auto: LED ON (dark & motion detected)\n");
      }
    }
    last_pir_off_time = 0;
  } else if (pir == 0) {
    /* 无人状态 */
    if (last_pir_off_time == 0) {
      last_pir_off_time = now;
    }
    /* 延时关闭LED */
    if (led_state && (now - last_pir_off_time) >= PIR_OFF_DELAY) {
      if (rpc_relay2_control(0) == 0) {
        led_state = 0;
        printf("Auto: LED OFF (no motion for %d seconds)\n", PIR_OFF_DELAY);
      }
    }
  }
}

/**
 * @brief 自动控制主函数
 *
 * 读取传感器数据并执行三种联动控制：
 * 1. 烟雾联动（最高优先级）
 * 2. 温度联动
 * 3. 光照+PIR联动
 */
static void auto_control(void) {
  int pir, light, smoke_digital;
  int temp = -1;
  static time_t smoke_fan_until = 0;
  static time_t last_alert_time = 0;

  /* 使用缓存的传感器数据（由refresh_sensor_cache填充） */
  if (!g_sensor_cache.valid) {
    return;
  }
  pir = g_sensor_cache.pir;
  light = g_sensor_cache.light;
  temp = g_sensor_cache.temp;
  smoke_digital = g_sensor_cache.smoke_digital;

  /* 执行自动控制 */
  auto_control_smoke(smoke_digital, &smoke_fan_until, &last_alert_time);
  auto_control_temp(temp, smoke_digital, smoke_fan_until);
  auto_control_light_pir(light, pir);
}

/* ========================================================================== */
/*                              MQTT连接管理 */
/* ========================================================================== */

/** @brief 前向声明：消息处理回调函数 */
static void message_handler(void *arg, message_data_t *msg);

/**
 * @brief MQTT重连函数
 * @return 0成功, -1失败
 *
 * 尝试重新连接MQTT服务器并重新订阅主题。
 */
static int mqtt_reconnect(void) {
  int err;

  for (int i = 0; i < MQTT_RECONNECT_MAX_RETRIES; i++) {
    printf("MQTT reconnecting... attempt %d/%d\n", i + 1,
           MQTT_RECONNECT_MAX_RETRIES);

    err = mqtt_connect(client);
    if (err == 0) {
      printf("MQTT reconnected successfully\n");
      mqtt_connected = 1;

      /* 重新订阅控制主题 */
      err = mqtt_subscribe(client, CMD_TOPIC, QOS1, message_handler);
      if (err != 0) {
        printf("mqtt_subscribe failed after reconnect: %d\n", err);
        return -1;
      }
      printf("Re-subscribed to %s\n", CMD_TOPIC);
      return 0;
    }

    printf("mqtt_connect failed: %d, retrying in %d seconds...\n", err,
           MQTT_RECONNECT_DELAY);
    sleep(MQTT_RECONNECT_DELAY);
  }

  printf("MQTT reconnect failed after %d attempts\n",
         MQTT_RECONNECT_MAX_RETRIES);
  return -1;
}

/* ========================================================================== */
/*                              遥测线程 */
/* ========================================================================== */

/**
 * @brief 遥测线程函数
 * @param arg 线程参数（未使用）
 * @return NULL
 *
 * 定时执行：
 * 1. 检查MQTT连接状态，断开则重连
 * 2. 发布遥测数据
 * 3. 执行自动控制
 */
static void *telemetry_thread_func(void *arg) {
  (void)arg;
  static time_t last_heartbeat_time = 0;

  while (running) {
    sleep(TELEMETRY_INTERVAL);

    /* 检查MQTT连接 */
    if (!mqtt_connected) {
      if (mqtt_reconnect() != 0) {
        continue;
      }
    }

    /* 执行遥测和控制 */
    if (client && mqtt_connected) {
      refresh_sensor_cache();
      publish_telemetry();
      auto_control();

      /* 心跳上报 */
      time_t now = time(NULL);
      if (now - last_heartbeat_time >= HEARTBEAT_INTERVAL) {
        publish_heartbeat();
        last_heartbeat_time = now;
      }
    }
  }

  return NULL;
}

/* ========================================================================== */
/*                              RPC处理函数 */
/* ========================================================================== */

/**
 * @brief 处理RPC读取请求
 * @param method 方法名
 * @param rpc_func RPC读取函数指针
 * @param key 响应数据键名
 */
static void handle_rpc_read(const char *method, int (*rpc_func)(int *),
                            const char *key) {
  int value;
  int ret = rpc_func(&value);

  if (ret == 0) {
    char extra[64];
    sprintf(extra, "{\"%s\":%d}", key, value);
    publish_response(method, 1, extra);
  } else {
    publish_response(method, 0, "read failed");
  }
}

/**
 * @brief 处理RPC控制请求
 * @param method 方法名
 * @param params_obj 参数对象
 * @param rpc_func RPC控制函数指针
 * @param state 状态指针（可选，用于更新本地状态）
 */
static void handle_rpc_control(const char *method, cJSON *params_obj,
                               int (*rpc_func)(int), int *state) {
  if (params_obj && cJSON_IsArray(params_obj)) {
    cJSON *on = cJSON_GetArrayItem(params_obj, 0);
    if (on && cJSON_IsNumber(on)) {
      int ret = rpc_func(on->valueint);
      if (ret == 0 && state) {
        *state = on->valueint;
      }
      publish_response(method, (ret == 0) ? 1 : 0, NULL);
    } else {
      publish_response(method, 0, "invalid param");
    }
  } else {
    publish_response(method, 0, "invalid params");
  }
}

/* ========================================================================== */
/*                              MQTT消息处理 */
/* ========================================================================== */

/**
 * @brief 处理MQTT消息负载（在工作线程中执行）
 * @param payload JSON格式的消息负载
 *
 * 解析控制指令并转发到本地RPC服务。
 */
static void process_message_payload(const char *payload) {
  printf("Processing command: %s\n", payload);

  /* 解析JSON */
  cJSON *root = cJSON_Parse(payload);
  if (!root) {
    printf("JSON parse error\n");
    return;
  }

  /* 获取方法名和参数 */
  cJSON *method_obj = cJSON_GetObjectItem(root, "method");
  cJSON *params_obj = cJSON_GetObjectItem(root, "params");

  if (!method_obj || !cJSON_IsString(method_obj)) {
    printf("No method field\n");
    cJSON_Delete(root);
    return;
  }

  const char *method = method_obj->valuestring;

  /* 分发处理 */
  if (strcmp(method, "led_control") == 0) {
    handle_rpc_control(method, params_obj, rpc_led_control, NULL);
  } else if (strcmp(method, "dht11_read") == 0) {
    char humi, temp;
    int ret = rpc_dht11_read(&humi, &temp);
    if (ret == 0) {
      char extra[64];
      sprintf(extra, "{\"humi\":%d,\"temp\":%d}", humi, temp);
      publish_response(method, 1, extra);
    } else {
      publish_response(method, 0, "read failed");
    }
  } else if (strcmp(method, "pir_read") == 0) {
    handle_rpc_read(method, rpc_pir_read, "pir");
  } else if (strcmp(method, "light_read") == 0) {
    handle_rpc_read(method, rpc_light_read, "light");
  } else if (strcmp(method, "relay_control") == 0) {
    handle_rpc_control(method, params_obj, rpc_relay_control, &fan_state);
  } else if (strcmp(method, "relay2_control") == 0) {
    handle_rpc_control(method, params_obj, rpc_relay2_control, &led_state);
  } else if (strcmp(method, "smoke_digital_read") == 0) {
    handle_rpc_read(method, rpc_smoke_digital_read, "smoke_digital");
  } else if (strcmp(method, "system_status") == 0) {
    /* 查询系统状态 */
    sysmon_status_t sys_status;
    if (sysmon_get_status(&sys_status) == SYSMON_OK) {
      char buf[512];
      if (sysmon_status_to_json(&sys_status, buf, sizeof(buf)) == SYSMON_OK) {
        publish_response(method, 1, buf);
      } else {
        publish_response(method, 0, "format error");
      }
    } else {
      publish_response(method, 0, "read failed");
    }
  } else if (strcmp(method, "sensor_status") == 0) {
    /* 查询传感器状态 - 使用sensor_manager */
    char buf[512];
    int offset = 0;
    offset += snprintf(buf + offset, sizeof(buf) - offset, "{");

    const char *names[] = {"dht11", "pir", "light", "smoke"};
    for (int i = 0; i < 4; i++) {
      sensor_info_t info;
      if (sensor_mgr_get_info(names[i], &info) == SMGR_OK) {
        const char *status_str = info.enabled ? "online" : "disabled";
        offset += snprintf(buf + offset, sizeof(buf) - offset,
                           "%s\"%s\":{\"status\":\"%s\",\"failures\":%d}",
                           (i > 0) ? "," : "", names[i], status_str, info.failure_count);
      } else {
        offset += snprintf(buf + offset, sizeof(buf) - offset,
                           "%s\"%s\":{\"status\":\"unknown\",\"failures\":0}",
                           (i > 0) ? "," : "", names[i]);
      }
    }
    snprintf(buf + offset, sizeof(buf) - offset, "}");
    publish_response(method, 1, buf);
  } else if (strcmp(method, "ota_upgrade") == 0) {
    /* 触发OTA升级 */
    if (params_obj && cJSON_IsObject(params_obj)) {
      ota_info_t ota_info;
      memset(&ota_info, 0, sizeof(ota_info));

      cJSON *ver = cJSON_GetObjectItem(params_obj, "version");
      cJSON *url = cJSON_GetObjectItem(params_obj, "url");
      cJSON *checksum = cJSON_GetObjectItem(params_obj, "checksum");
      cJSON *checksum_type = cJSON_GetObjectItem(params_obj, "checksum_type");

      if (ver && cJSON_IsString(ver)) strncpy(ota_info.version, ver->valuestring, OTA_VERSION_MAX_LEN - 1);
      if (url && cJSON_IsString(url)) strncpy(ota_info.url, url->valuestring, OTA_URL_MAX_LEN - 1);
      if (checksum && cJSON_IsString(checksum)) strncpy(ota_info.checksum, checksum->valuestring, OTA_CHECKSUM_MAX_LEN - 1);
      if (checksum_type && cJSON_IsNumber(checksum_type)) ota_info.checksum_type = checksum_type->valueint;

      ota_error_t ret = ota_start_upgrade(&ota_info);
      if (ret == OTA_OK) {
        publish_response(method, 1, "upgrade started");
        audit_log_event(AUDIT_EVENT_FIRMWARE_UPDATE, AUDIT_LEVEL_INFO,
                        "mqtt", "remote", "OTA upgrade started",
                        url ? url->valuestring : "");
      } else {
        publish_response(method, 0, ota_get_error_string(ret));
      }
    } else {
      publish_response(method, 0, "invalid params");
    }
  } else if (strcmp(method, "ota_rollback") == 0) {
    /* 回滚固件 */
    ota_error_t ret = ota_rollback();
    if (ret == OTA_OK) {
      publish_response(method, 1, "rollback started");
    } else {
      publish_response(method, 0, ota_get_error_string(ret));
    }
  } else if (strcmp(method, "firmware_version") == 0) {
    /* 查询固件版本 */
    char version[OTA_VERSION_MAX_LEN];
    if (ota_get_current_version(version, sizeof(version)) == OTA_OK) {
      char extra[64];
      snprintf(extra, sizeof(extra), "{\"version\":\"%s\"}", version);
      publish_response(method, 1, extra);
    } else {
      publish_response(method, 0, "read failed");
    }
  } else if (strcmp(method, "config_update") == 0) {
    /* 远程配置更新（通过配置模块统一管理） */
    if (params_obj && cJSON_IsObject(params_obj)) {
      int updated = 0;

      /* 遍历所有键值对，调用config_update_int更新 */
      cJSON *child = params_obj->child;
      while (child) {
        if (child->string && cJSON_IsNumber(child)) {
          if (config_update_int(&app_config, child->string, child->valueint) == 0) {
            /* 同步更新局部变量以保持兼容 */
            if (strcmp(child->string, "temp_high") == 0) temp_high_threshold = child->valueint;
            else if (strcmp(child->string, "temp_low") == 0) temp_low_threshold = child->valueint;
            else if (strcmp(child->string, "smoke_alert_level") == 0) smoke_alert_level = child->valueint;
            else if (strcmp(child->string, "smoke_fan_duration") == 0) smoke_fan_duration = child->valueint;
            else if (strcmp(child->string, "smoke_alert_interval") == 0) smoke_alert_interval = child->valueint;
            updated++;
          }
        }
        child = child->next;
      }

      char extra[64];
      snprintf(extra, sizeof(extra), "{\"updated\":%d}", updated);
      publish_response(method, 1, extra);
      printf("Config updated: %d parameters\n", updated);
      audit_log_event(AUDIT_EVENT_CONFIG_CHANGE, AUDIT_LEVEL_INFO,
                      "mqtt", "remote", "Remote config update", payload);
    } else {
      publish_response(method, 0, "invalid params");
    }
  } else if (strcmp(method, "log_level") == 0) {
    /* 动态调整日志级别 */
    if (params_obj && cJSON_IsArray(params_obj)) {
      cJSON *level = cJSON_GetArrayItem(params_obj, 0);
      if (level && cJSON_IsNumber(level)) {
        int lvl = level->valueint;
        if (lvl >= LOG_LEVEL_DEBUG && lvl <= LOG_LEVEL_NONE) {
          log_set_level((log_level_t)lvl);
          publish_response(method, 1, NULL);
          printf("Log level set to %d\n", lvl);
        } else {
          publish_response(method, 0, "invalid level");
        }
      } else {
        publish_response(method, 0, "invalid param");
      }
    } else {
      publish_response(method, 0, "invalid params");
    }
  } else if (strcmp(method, "camera_capture") == 0) {
    /* 远程抓拍 */
    char filename[64];
    snprintf(filename, sizeof(filename), "/tmp/capture_%ld.jpg", time(NULL));

    if (rpc_camera_capture_jpeg(filename) == 0) {
      char extra[128];
      snprintf(extra, sizeof(extra), "{\"file\":\"%s\"}", filename);
      publish_response(method, 1, extra);
      printf("Camera captured via RPC: %s\n", filename);
      publish_image("manual_capture");
    } else {
      publish_response(method, 0, "capture failed");
      printf("Camera capture failed\n");
    }
  } else if (strcmp(method, "device_restart") == 0) {
    /* 远程重启设备 */
    publish_response(method, 1, "restarting");
    printf("Remote restart requested\n");
    running = 0;
  } else {
    printf("Unknown method: %s\n", method);
    publish_response(method, 0, "unknown method");
  }

  cJSON_Delete(root);
}

/* ========================================================================== */
/*                              信号处理 */
/* ========================================================================== */

/**
 * @brief 信号处理函数
 * @param sig 信号编号
 */
static void signal_handler(int sig) {
  (void)sig;
  printf("\nReceived signal %d, exiting...\n", sig);
  running = 0;
  cmd_worker_running = 0;
  if (cmd_queue) {
    msgq_close(cmd_queue);
  }
}

/* ========================================================================== */
/*                              消息队列工作线程 */
/* ========================================================================== */

/**
 * @brief 命令工作线程函数
 * @param arg 线程参数（未使用）
 * @return NULL
 *
 * 从消息队列中取出MQTT命令并执行RPC调用，
 * 避免在MQTT回调线程中执行耗时的RPC操作。
 */
static void *cmd_worker_thread(void *arg) {
  (void)arg;
  msg_t msg;

  printf("Command worker thread started\n");

  while (cmd_worker_running) {
    msgq_error_t ret = msgq_receive(cmd_queue, &msg, 1000);
    if (ret == MSGQ_OK) {
      process_message_payload(msg.payload);
    } else if (ret == MSGQ_ERROR_TIMEOUT) {
      continue;
    } else {
      break;
    }
  }

  printf("Command worker thread stopped\n");
  return NULL;
}

/**
 * @brief MQTT消息处理回调
 * @param arg 用户参数（未使用）
 * @param msg 消息数据
 *
 * 将接收到的MQTT消息放入命令队列，由工作线程异步处理。
 * 回调快速返回，不阻塞mqttclient网络线程。
 */
static void message_handler(void *arg, message_data_t *msg) {
  (void)arg;
  char *payload = (char *)msg->message->payload;
  int payload_len = msg->message->payloadlen;

  if (!cmd_queue || payload_len <= 0 || payload_len >= MSG_PAYLOAD_MAX_SIZE) {
    printf("Cannot enqueue: queue=%p, len=%d\n", (void *)cmd_queue, payload_len);
    return;
  }

  /* 根据方法名确定优先级 */
  msg_prio_t prio = MSG_PRIO_NORMAL;
  if (strstr(payload, "smoke") || strstr(payload, "device_restart")) {
    prio = MSG_PRIO_URGENT;
  } else if (strstr(payload, "ota_")) {
    prio = MSG_PRIO_HIGH;
  }

  msg_t enq_msg;
  memset(&enq_msg, 0, sizeof(enq_msg));
  enq_msg.type = MSG_TYPE_CONTROL;
  enq_msg.priority = prio;
  enq_msg.payload_len = payload_len;
  memcpy(enq_msg.payload, payload, payload_len);
  enq_msg.payload[payload_len] = '\0';

  msgq_error_t ret = msgq_try_send(cmd_queue, &enq_msg);
  if (ret != MSGQ_OK) {
    printf("Command queue full, dropping message: %.*s\n",
           payload_len < 80 ? payload_len : 80, payload);
  }
}

/* ========================================================================== */
/*                              主函数 */
/* ========================================================================== */

/**
 * @brief 程序入口
 * @return 0成功, -1失败
 *
 * 初始化流程：
 * 1. 加载配置（环境变量 + 配置文件）
 * 2. 初始化看门狗
 * 3. 初始化数据缓存、系统监控、设备认证、OTA管理器
 * 4. 连接本地RPC服务器
 * 5. 连接MQTT服务器并订阅控制主题
 * 6. 创建命令消息队列并启动工作线程
 * 7. 启动遥测线程
 * 8. 等待退出信号
 */
int main(void) {
  int err;
  pthread_t telemetry_tid;
  pthread_t cmd_tid;

  /* 加载配置 */
  if (load_mqtt_config() < 0) {
    return -1;
  }
  if (config_load_combined("config.json", &app_config) == 0) {
    smoke_alert_level = app_config.thresholds.smoke_alert_level;
    smoke_fan_duration = app_config.thresholds.smoke_fan_duration;
    smoke_alert_interval = app_config.thresholds.smoke_alert_interval;
    temp_high_threshold = app_config.thresholds.temp_high;
    temp_low_threshold = app_config.thresholds.temp_low;
    printf("Smoke thresholds loaded: level=%d, fan_duration=%d, alert_interval=%d\n",
           smoke_alert_level, smoke_fan_duration, smoke_alert_interval);
  }
  printf("MQTT config loaded from environment variables\n");

  /* 初始化看门狗（超时60秒，MQTT操作可能较慢） */
  watchdog_error_t wd_ret = watchdog_init(60, NULL, NULL);
  if (wd_ret != WATCHDOG_OK) {
    printf("Watchdog initialization failed: %s\n",
           watchdog_get_error_string(wd_ret));
    return -1;
  }

  /* 启动看门狗 */
  wd_ret = watchdog_start();
  if (wd_ret != WATCHDOG_OK) {
    printf("Watchdog start failed: %s\n", watchdog_get_error_string(wd_ret));
    watchdog_cleanup();
    return -1;
  }

  /* 初始化数据缓存 */
  if (data_cache_init() != CACHE_OK) {
    printf("Data cache initialization failed\n");
    watchdog_cleanup();
    return -1;
  }
  printf("Data cache initialized\n");

  /* 初始化系统监控 */
  if (sysmon_init() != SYSMON_OK) {
    printf("System monitor initialization failed\n");
    watchdog_cleanup();
    return -1;
  }
  printf("System monitor initialized\n");

  /* 初始化设备认证模块 */
  if (devauth_init() != DEVAUTH_OK) {
    printf("Device auth initialization failed\n");
    watchdog_cleanup();
    return -1;
  }

  /* 加载或注册设备凭证 */
  devauth_info_t auth_info;
  if (devauth_load_credentials(&auth_info) != DEVAUTH_OK) {
    printf("Device not registered, registering...\n");
    if (devauth_register_device(&auth_info) != DEVAUTH_OK) {
      printf("Device registration failed\n");
      watchdog_cleanup();
      return -1;
    }
  }
  printf("Device ID: %s\n", auth_info.device_id);

  /* 初始化OTA管理器 */
  if (ota_init() != OTA_OK) {
    printf("OTA manager initialization failed\n");
  } else {
    printf("OTA manager initialized\n");
  }

  /* 摄像头由 rpc_server 管理，mqtt_bridge 通过 RPC 调用拍照 */
  printf("Camera will be accessed via RPC (managed by rpc_server)\n");

  /* 初始化安全审计模块 */
  if (security_audit_init() == AUDIT_OK) {
    printf("Security audit initialized\n");
  } else {
    printf("Security audit initialization failed (non-fatal)\n");
  }

  /* 初始化性能监控模块 */
  if (perf_monitor_init() == PERF_OK) {
    perf_threshold_t thresh = {80.0, 85.0, 1000.0, 60};
    perf_threshold_set(&thresh);
    printf("Performance monitor initialized\n");
  } else {
    printf("Performance monitor initialization failed (non-fatal)\n");
  }

  /* 初始化内存跟踪模块 */
  if (mem_track_init() == MEMPOOL_OK) {
    printf("Memory tracking initialized\n");
  } else {
    printf("Memory tracking initialization failed (non-fatal)\n");
  }

  /* 连接RPC服务器 */
  if (RPC_Client_Init() < 0) {
    printf("Failed to connect to rpc_server at 127.0.0.1:1234\n");
    watchdog_cleanup();
    return -1;
  }
  printf("RPC client connected to rpc_server\n");

  /* 初始化MQTT客户端 */
  client = mqtt_lease();
  if (!client) {
    printf("mqtt_lease failed\n");
    watchdog_cleanup();
    return -1;
  }

  /* 配置MQTT参数 */
  mqtt_set_host(client, (char *)mqtt_host);

  /* TLS支持：如果启用TLS，使用8883端口并设置CA证书 */
  if (auth_info.use_tls) {
    const char *tls_port = "8883";
    mqtt_set_port(client, (char *)tls_port);
    if (strlen(auth_info.ca_cert_path) > 0) {
      mqtt_set_ca(client, (char *)auth_info.ca_cert_path);
      printf("TLS enabled, CA cert: %s\n", auth_info.ca_cert_path);
    } else {
      printf("Warning: TLS enabled but no CA cert configured\n");
    }
  } else {
    mqtt_set_port(client, (char *)mqtt_port);
  }

  /* 使用设备ID作为客户端ID（如果环境变量未设置） */
  if (mqtt_clientid && strlen(mqtt_clientid) > 0) {
    mqtt_set_client_id(client, (char *)mqtt_clientid);
  } else {
    mqtt_set_client_id(client, auth_info.device_id);
  }

  /* 使用设备Token作为密码（如果环境变量未设置） */
  if (mqtt_username && strlen(mqtt_username) > 0) {
    mqtt_set_user_name(client, (char *)mqtt_username);
  }
  if (mqtt_password && strlen(mqtt_password) > 0) {
    mqtt_set_password(client, (char *)mqtt_password);
  } else {
    mqtt_set_password(client, auth_info.device_token);
  }

  mqtt_set_clean_session(client, 1);

  /* 连接MQTT服务器 */
  err = mqtt_connect(client);
  if (err != 0) {
    printf("mqtt_connect failed: %d\n", err);
    watchdog_cleanup();
    return -1;
  }
  mqtt_connected = 1;
  printf("MQTT connected to %s:%s\n", mqtt_host, mqtt_port);

  /* 订阅控制主题 */
  err = mqtt_subscribe(client, CMD_TOPIC, QOS1, message_handler);
  if (err != 0) {
    printf("mqtt_subscribe failed: %d\n", err);
    watchdog_cleanup();
    return -1;
  }
  printf("Subscribed to %s\n", CMD_TOPIC);

  /* 创建命令消息队列 */
  cmd_queue = msgq_create(64);
  if (!cmd_queue) {
    printf("Failed to create command queue\n");
    watchdog_cleanup();
    return -1;
  }
  printf("Command queue created (capacity=64)\n");

  /* 启动命令工作线程 */
  if (pthread_create(&cmd_tid, NULL, cmd_worker_thread, NULL) != 0) {
    printf("Failed to create command worker thread\n");
    msgq_destroy(cmd_queue);
    watchdog_cleanup();
    return -1;
  }

  /* 启动遥测线程 */
  if (pthread_create(&telemetry_tid, NULL, telemetry_thread_func, NULL) != 0) {
    printf("Failed to create telemetry thread\n");
    watchdog_cleanup();
    return -1;
  }

  /* 注册信号处理 */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* 主循环等待退出 */
  while (running) {
    watchdog_feed(); /* 喂狗，表明程序正常运行 */
    sleep(1);
  }

  /* 清理资源 */
  cmd_worker_running = 0;
  if (cmd_queue) {
    msgq_close(cmd_queue);
  }
  pthread_cancel(telemetry_tid);
  pthread_join(telemetry_tid, NULL);
  pthread_join(cmd_tid, NULL);
  if (cmd_queue) {
    msgq_destroy(cmd_queue);
    cmd_queue = NULL;
  }

  /* 保存缓存数据到文件 */
  data_cache_save_to_file();
  data_cache_cleanup();

  ota_cleanup();
  security_audit_cleanup();
  perf_monitor_cleanup();
  mem_track_print_leaks();
  mem_track_cleanup();
  sensor_mgr_cleanup();

  mqtt_disconnect(client);
  watchdog_cleanup();
  printf("Exiting\n");

  return 0;
}
