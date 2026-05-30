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

/* 看门狗和数据缓存模块是C语言实现，需要extern "C"声明 */
extern "C" {
#include "watchdog.h"
#include "data_cache.h"
#include "config.h"
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

/* ========================================================================== */
/*                              系统参数定义 */
/* ========================================================================== */

/** @brief 遥测数据上报间隔（秒） */
#define TELEMETRY_INTERVAL 5

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
  char raw_humi, raw_temp;
  int need_report = 0;

  /* 读取所有传感器数据 */
  if (rpc_pir_read(&pir) != 0)
    pir = -1;
  if (rpc_light_read(&light) != 0)
    light = -1;
  if (rpc_dht11_read(&raw_humi, &raw_temp) == 0) {
    humi = (int)raw_humi;
    temp = (int)raw_temp;
  }
  if (rpc_relay_read(&relay) != 0)
    relay = -1;
  if (rpc_relay2_read(&relay2) != 0)
    relay2 = -1;
  if (rpc_smoke_digital_read(&smoke_digital) != 0)
    smoke_digital = -1;

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

    /* 定时发送告警 */
    if (now - *last_alert_time > smoke_alert_interval) {
      publish_alert("smoke", 1);
      *last_alert_time = now;
    }
  } else {
    /* 烟雾恢复正常 */
    smoke_alarm_start_time = 0;
    smoke_sensor_fault = 0;

    /* 风扇运行时间到期后关闭风扇 */
    if (fan_state && now >= *smoke_fan_until) {
      if (rpc_relay_control(0) == 0) {
        fan_state = 0;
        printf("Auto: Fan OFF, smoke cleared\n");
      }
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
  char raw_humi, raw_temp;
  static time_t smoke_fan_until = 0;
  static time_t last_alert_time = 0;

  /* 读取传感器数据 */
  if (rpc_pir_read(&pir) != 0)
    pir = -1;
  if (rpc_light_read(&light) != 0)
    light = -1;
  if (rpc_dht11_read(&raw_humi, &raw_temp) == 0) {
    temp = (int)raw_temp;
  }
  if (rpc_smoke_digital_read(&smoke_digital) != 0)
    smoke_digital = -1;

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
      publish_telemetry();
      auto_control();
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
 * @brief MQTT消息处理回调
 * @param arg 用户参数（未使用）
 * @param msg 消息数据
 *
 * 处理接收到的MQTT控制指令，转发到本地RPC服务。
 * 支持的方法：
 *   - led_control: LED控制
 *   - dht11_read: 温湿度读取
 *   - pir_read: PIR读取
 *   - light_read: 光照读取
 *   - relay_control: 继电器1控制（风扇）
 *   - relay2_control: 继电器2控制（LED灯）
 *   - smoke_digital_read: 烟雾传感器读取
 */
static void message_handler(void *arg, message_data_t *msg) {
  (void)arg;
  char *payload = (char *)msg->message->payload;
  printf("Received MQTT: %s\n", payload);

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
}

/* ========================================================================== */
/*                              主函数 */
/* ========================================================================== */

/**
 * @brief 程序入口
 * @return 0成功, -1失败
 *
 * 初始化流程：
 * 1. 加载MQTT配置（从环境变量）
 * 2. 连接本地RPC服务器
 * 3. 连接MQTT服务器
 * 4. 订阅控制主题
 * 5. 启动遥测线程
 * 6. 等待退出信号
 */
int main(void) {
  int err;
  pthread_t telemetry_tid;

  /* 加载配置 */
  app_config_t cfg;
  if (load_mqtt_config() < 0) {
    return -1;
  }
  if (config_load_combined("config.json", &cfg) == 0) {
    smoke_alert_level = cfg.thresholds.smoke_alert_level;
    smoke_fan_duration = cfg.thresholds.smoke_fan_duration;
    smoke_alert_interval = cfg.thresholds.smoke_alert_interval;
    temp_high_threshold = cfg.thresholds.temp_high;
    temp_low_threshold = cfg.thresholds.temp_low;
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
  mqtt_set_port(client, (char *)mqtt_port);
  mqtt_set_client_id(client, (char *)mqtt_clientid);
  mqtt_set_user_name(client, (char *)mqtt_username);
  mqtt_set_password(client, (char *)mqtt_password);
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
  pthread_cancel(telemetry_tid);
  pthread_join(telemetry_tid, NULL);

  /* 保存缓存数据到文件 */
  data_cache_save_to_file();
  data_cache_cleanup();

  mqtt_disconnect(client);
  watchdog_cleanup();
  printf("Exiting\n");

  return 0;
}
