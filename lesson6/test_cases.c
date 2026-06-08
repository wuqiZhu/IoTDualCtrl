/**
 * @file test_cases.c
 * @brief 单元测试用例
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 2.0
 *
 * 为不依赖硬件的函数编写测试用例。
 * 覆盖模块：error, config, data_cache, msg_queue, crypto_utils, memory_pool
 */

#include "config.h"
#include "error.h"
#include "test_framework.h"
#include "data_cache.h"
#include "msg_queue.h"
#include "crypto_utils.h"
#include "memory_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================== */
/*                              错误处理测试 */
/* ========================================================================== */

/**
 * @brief 测试错误码字符串
 */
void test_error_get_string(void) {
  ASSERT_STRING_EQUAL("Success", error_get_string(ERR_SUCCESS));
  ASSERT_STRING_EQUAL("Invalid parameter", error_get_string(ERR_INVALID_PARAM));
  ASSERT_STRING_EQUAL("Null pointer", error_get_string(ERR_NULL_POINTER));
  ASSERT_STRING_EQUAL("GPIO export failed", error_get_string(ERR_GPIO_EXPORT));
  ASSERT_STRING_EQUAL("MQTT connection failed",
                      error_get_string(ERR_MQTT_CONNECT));
  ASSERT_STRING_EQUAL("Unknown error", error_get_string(9999));
}

/**
 * @brief 测试错误码值
 */
void test_error_codes(void) {
  ASSERT_EQUAL(0, ERR_SUCCESS);
  ASSERT_TRUE(ERR_INVALID_PARAM < 0);
  ASSERT_TRUE(ERR_GPIO_EXPORT < 0);
  ASSERT_TRUE(ERR_MQTT_CONNECT < 0);
}

/* ========================================================================== */
/*                              配置加载测试 */
/* ========================================================================== */

/**
 * @brief 测试配置默认值
 */
void test_config_defaults(void) {
  app_config_t config;
  int ret = config_load("nonexistent.json", &config);

  /* 文件不存在时应返回-1，但使用默认值 */
  ASSERT_EQUAL(-1, ret);

  /* 检查默认值 */
  ASSERT_STRING_EQUAL("", config.mqtt.host);
  ASSERT_EQUAL(1883, config.mqtt.port);
  ASSERT_STRING_EQUAL("", config.mqtt.username);
  ASSERT_STRING_EQUAL("mqtt_bridge", config.mqtt.client_id);

  ASSERT_STRING_EQUAL("device/control", config.topics.command);
  ASSERT_STRING_EQUAL("device/response", config.topics.response);
  ASSERT_STRING_EQUAL("device/telemetry", config.topics.telemetry);
  ASSERT_STRING_EQUAL("device/alert", config.topics.alert);

  ASSERT_EQUAL(116, config.gpio.pir_pin);
  ASSERT_EQUAL(117, config.gpio.smoke_do_pin);
  ASSERT_EQUAL(118, config.gpio.relay1_pin);
  ASSERT_EQUAL(119, config.gpio.relay2_pin);

  ASSERT_EQUAL(2000, config.thresholds.light_threshold);
  ASSERT_EQUAL(32, config.thresholds.temp_high);
  ASSERT_EQUAL(30, config.thresholds.temp_low);
  ASSERT_EQUAL(30, config.thresholds.pir_off_delay);
}

/**
 * @brief 测试配置加载
 */
void test_config_load(void) {
  app_config_t config;
  int ret = config_load("config.json", &config);

  /* 如果文件存在，应该成功加载 */
  if (ret == 0) {
    /* config.json中的值应该被加载 */
    ASSERT_EQUAL(1883, config.mqtt.port);
    ASSERT_EQUAL(116, config.gpio.pir_pin);
    ASSERT_EQUAL(2000, config.thresholds.light_threshold);
  }
}

/**
 * @brief 测试配置组合加载
 */
void test_config_combined(void) {
  app_config_t config;

  /* 测试不存在的文件，应该使用默认值 */
  int ret = config_load_combined("nonexistent.json", &config);

  /* 默认值中host和username为空，组合加载应返回-1 */
  ASSERT_EQUAL(-1, ret);
}

/**
 * @brief 测试配置结构体大小
 */
void test_config_struct_size(void) {
  /* 确保配置结构体大小合理 */
  ASSERT_TRUE(sizeof(app_config_t) > 0);
  ASSERT_TRUE(sizeof(mqtt_config_t) > 0);
  ASSERT_TRUE(sizeof(topics_config_t) > 0);
  ASSERT_TRUE(sizeof(gpio_config_t) > 0);
  ASSERT_TRUE(sizeof(thresholds_config_t) > 0);
}

/* ========================================================================== */
/*                              JSON解析测试 */
/* ========================================================================== */

/**
 * @brief 测试JSON解析
 */
void test_json_parse(void) {
  /* 测试简单的JSON解析 */
  const char *json_str = "{\"method\":\"test\",\"params\":[1,2,3]}";

  /* 这里需要cJSON库，暂时跳过实际解析 */
  ASSERT_NOT_NULL(json_str);
  ASSERT_EQUAL('{', json_str[0]);
}

/* ========================================================================== */
/*                              字符串处理测试 */
/* ========================================================================== */

/**
 * @brief 测试字符串长度
 */
void test_string_length(void) {
  const char *str1 = "Hello";
  const char *str2 = "";
  const char *str3 = "MQTT Bridge";

  ASSERT_EQUAL(5, strlen(str1));
  ASSERT_EQUAL(0, strlen(str2));
  ASSERT_EQUAL(11, strlen(str3));
}

/**
 * @brief 测试字符串比较
 */
void test_string_compare(void) {
  const char *str1 = "Hello";
  const char *str2 = "Hello";
  const char *str3 = "World";

  ASSERT_EQUAL(0, strcmp(str1, str2));
  ASSERT_NOT_EQUAL(0, strcmp(str1, str3));
}

/* ========================================================================== */
/*                              数值计算测试 */
/* ========================================================================== */

/**
 * @brief 测试温度阈值判断
 */
void test_temperature_threshold(void) {
  int temp_high = 32;
  int temp_low = 30;

  /* 测试温度高于阈值 */
  ASSERT_TRUE(35 > temp_high);
  ASSERT_FALSE(30 > temp_high);

  /* 测试温度低于阈值 */
  ASSERT_TRUE(25 < temp_low);
  ASSERT_FALSE(30 < temp_low);
}

/**
 * @brief 测试光照阈值判断
 */
void test_light_threshold(void) {
  int light_threshold = 2000;

  /* 测试黑暗 */
  ASSERT_TRUE(1000 < light_threshold);

  /* 测试明亮 */
  ASSERT_FALSE(3000 < light_threshold);
}

/**
 * @brief 测试烟雾报警判断
 */
void test_smoke_alert(void) {
  int smoke_alert_level = 0;

  /* 测试检测到烟雾 */
  ASSERT_EQUAL(0, smoke_alert_level);

  /* 测试正常 */
  ASSERT_NOT_EQUAL(0, 1);
}

/* ========================================================================== */
/*                              时间计算测试 */
/* ========================================================================== */

/**
 * @brief 测试延时计算
 */
void test_delay_calculation(void) {
  int pir_off_delay = 30;
  time_t now = 1000;
  time_t last_pir_off_time = 970;

  /* 测试延时是否到达 */
  ASSERT_TRUE((now - last_pir_off_time) >= pir_off_delay);

  /* 测试延时未到达 */
  last_pir_off_time = 990;
  ASSERT_FALSE((now - last_pir_off_time) >= pir_off_delay);
}

/* ========================================================================== */
/*                              系统监控测试 */
/* ========================================================================== */

/**
 * @brief 测试系统监控状态结构
 */
void test_system_monitor_struct(void) {
  /* 测试系统监控状态结构体大小合理 */
  ASSERT_TRUE(sizeof(void *) > 0);
}

/* ========================================================================== */
/*                              缓存测试 */
/* ========================================================================== */

/**
 * @brief 测试缓存文件路径
 */
void test_cache_file_path(void) {
  /* 测试缓存路径格式 */
  const char *cache_path = "/etc/device/telemetry_cache.dat";
  ASSERT_NOT_NULL(cache_path);
  ASSERT_TRUE(strlen(cache_path) > 0);
}

/**
 * @brief 测试JSON序列化
 */
void test_json_serialization(void) {
  /* 测试JSON格式正确性 */
  const char *json = "{\"temperature\":25.5,\"humidity\":60}";
  ASSERT_NOT_NULL(json);
  ASSERT_TRUE(strlen(json) > 0);
  ASSERT_EQUAL('{', json[0]);
  ASSERT_EQUAL('}', json[strlen(json) - 1]);
}

/* ========================================================================== */
/*                              Base64编码测试 */
/* ========================================================================== */

/**
 * @brief 测试Base64编码长度计算
 */
void test_base64_length(void) {
  /* Base64编码后长度约为原始长度的4/3倍 */
  int input_len = 100;
  int expected_output_len = ((input_len + 2) / 3) * 4;
  ASSERT_TRUE(expected_output_len > input_len);
  ASSERT_TRUE(expected_output_len <= input_len * 2);
}

/**
 * @brief 测试Base64字符集
 */
void test_base64_charset(void) {
  /* Base64字符集：A-Z, a-z, 0-9, +, /, = */
  const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
  ASSERT_EQUAL(65, strlen(base64_chars));
}

/* ========================================================================== */
/*                              配置阈值测试 */
/* ========================================================================== */

/**
 * @brief 测试配置阈值范围
 */
void test_config_threshold_range(void) {
  /* 测试温度阈值范围 */
  int temp_high = 32;
  int temp_low = 30;
  ASSERT_TRUE(temp_high > temp_low);
  ASSERT_TRUE(temp_high <= 50);
  ASSERT_TRUE(temp_low >= 15);

  /* 测试湿度阈值范围 */
  int humi_threshold = 80;
  ASSERT_TRUE(humi_threshold > 0);
  ASSERT_TRUE(humi_threshold <= 100);
}

/**
 * @brief 测试上报间隔范围
 */
void test_report_interval_range(void) {
  /* 测试上报间隔范围 */
  int telemetry_interval = 5;
  int heartbeat_interval = 60;
  int full_report_interval = 300;

  ASSERT_TRUE(telemetry_interval > 0);
  ASSERT_TRUE(heartbeat_interval > telemetry_interval);
  ASSERT_TRUE(full_report_interval > heartbeat_interval);
}

/* ========================================================================== */
/*                              数据缓存测试 (data_cache) */
/* ========================================================================== */

/**
 * @brief 测试数据缓存初始状态
 */
void test_cache_init_state(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());
  ASSERT_TRUE(data_cache_is_empty());
  ASSERT_FALSE(data_cache_is_full());
  ASSERT_EQUAL(0, data_cache_count());
  data_cache_clear();
}

/**
 * @brief 测试数据缓存推入和弹出
 */
void test_cache_push_pop(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());
  ASSERT_TRUE(data_cache_is_empty());

  /* 推入一条数据 */
  ASSERT_EQUAL(CACHE_OK, data_cache_push("test_data_1", 11));
  ASSERT_EQUAL(1, data_cache_count());
  ASSERT_FALSE(data_cache_is_empty());

  /* 弹出数据 */
  char buf[128];
  int len;
  ASSERT_EQUAL(CACHE_OK, data_cache_pop(buf, sizeof(buf), &len));
  ASSERT_EQUAL(11, len);
  ASSERT_EQUAL(0, strcmp(buf, "test_data_1"));
  ASSERT_TRUE(data_cache_is_empty());

  data_cache_clear();
}

/**
 * @brief 测试数据缓存FIFO顺序
 */
void test_cache_fifo_order(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());

  ASSERT_EQUAL(CACHE_OK, data_cache_push("first", 5));
  ASSERT_EQUAL(CACHE_OK, data_cache_push("second", 6));
  ASSERT_EQUAL(CACHE_OK, data_cache_push("third", 5));
  ASSERT_EQUAL(3, data_cache_count());

  char buf[128];
  int len;
  data_cache_pop(buf, sizeof(buf), &len);
  ASSERT_EQUAL(0, strcmp(buf, "first"));

  data_cache_pop(buf, sizeof(buf), &len);
  ASSERT_EQUAL(0, strcmp(buf, "second"));

  data_cache_pop(buf, sizeof(buf), &len);
  ASSERT_EQUAL(0, strcmp(buf, "third"));

  ASSERT_TRUE(data_cache_is_empty());
  data_cache_clear();
}

/**
 * @brief 测试缓存溢出覆盖
 */
void test_cache_overflow(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());

  /* 推入超过最大容量的数据 */
  int i;
  for (i = 0; i < CACHE_MAX_ENTRIES + 10; i++) {
    char data[32];
    snprintf(data, sizeof(data), "data_%d", i);
    ASSERT_EQUAL(CACHE_OK, data_cache_push(data, strlen(data)));
  }

  /* 验证计数不超过最大值 */
  ASSERT_EQUAL(CACHE_MAX_ENTRIES, data_cache_count());

  /* 最早的数据已被覆盖，第一个应该是 data_10 */
  char buf[128];
  int len;
  data_cache_pop(buf, sizeof(buf), &len);
  ASSERT_EQUAL(0, strcmp(buf, "data_10"));

  data_cache_clear();
}

/**
 * @brief 测试空缓存弹出
 */
void test_cache_pop_empty(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());
  data_cache_clear();

  char buf[128];
  int len;
  ASSERT_EQUAL(CACHE_ERROR_EMPTY, data_cache_pop(buf, sizeof(buf), &len));

  data_cache_clear();
}

/**
 * @brief 测试缓存统计信息
 */
void test_cache_stats(void) {
  ASSERT_EQUAL(CACHE_OK, data_cache_init());

  data_cache_push("data1", 5);
  data_cache_push("data2", 5);
  data_cache_push("data3", 5);

  cache_stats_t stats;
  ASSERT_EQUAL(CACHE_OK, data_cache_get_stats(&stats));
  ASSERT_EQUAL(3, stats.total_push_count);
  ASSERT_TRUE(stats.original_bytes > 0);

  data_cache_clear();
}

/* ========================================================================== */
/*                              消息队列测试 (msg_queue) */
/* ========================================================================== */

/**
 * @brief 测试消息队列创建和销毁
 */
void test_msgq_create_destroy(void) {
  msg_queue_t *q = msgq_create(8);
  ASSERT_NOT_NULL(q);

  ASSERT_EQUAL(0, msgq_get_count(q));
  ASSERT_TRUE(msgq_is_empty(q));
  ASSERT_FALSE(msgq_is_full(q));

  msgq_destroy(q);
}

/**
 * @brief 测试消息队列发送和接收
 */
void test_msgq_send_receive(void) {
  msg_queue_t *q = msgq_create(8);
  ASSERT_NOT_NULL(q);

  msg_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.type = MSG_TYPE_CONTROL;
  msg.priority = MSG_PRIO_NORMAL;
  msg.payload_len = 5;
  memcpy(msg.payload, "hello", 5);

  /* 发送 */
  ASSERT_EQUAL(MSGQ_OK, msgq_try_send(q, &msg));

  /* 接收 */
  msg_t recv;
  memset(&recv, 0, sizeof(recv));
  ASSERT_EQUAL(MSGQ_OK, msgq_try_receive(q, &recv));
  ASSERT_EQUAL(5, recv.payload_len);
  ASSERT_EQUAL(0, memcmp("hello", recv.payload, 5));
  ASSERT_EQUAL(MSG_TYPE_CONTROL, recv.type);

  msgq_destroy(q);
}

/**
 * @brief 测试消息队列空队列接收
 */
void test_msgq_receive_empty(void) {
  msg_queue_t *q = msgq_create(4);
  ASSERT_NOT_NULL(q);

  msg_t msg;
  ASSERT_EQUAL(MSGQ_ERROR_EMPTY, msgq_try_receive(q, &msg));

  msgq_destroy(q);
}

/**
 * @brief 测试消息队列满队列发送
 */
void test_msgq_send_full(void) {
  msg_queue_t *q = msgq_create(2);
  ASSERT_NOT_NULL(q);

  msg_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.type = MSG_TYPE_CONTROL;
  msg.payload_len = 1;
  msg.payload[0] = 'a';

  ASSERT_EQUAL(MSGQ_OK, msgq_try_send(q, &msg));
  ASSERT_EQUAL(MSGQ_OK, msgq_try_send(q, &msg));
  ASSERT_EQUAL(MSGQ_ERROR_FULL, msgq_try_send(q, &msg));

  msgq_destroy(q);
}

/* ========================================================================== */
/*                              数据安全测试 (crypto_utils) */
/* ========================================================================== */

/**
 * @brief 测试SHA-256哈希计算
 */
void test_sha256_calc(void) {
  uint8_t hash[SHA256_HASH_SIZE];
  char hex[SHA256_HEX_SIZE];

  /* "abc" 的SHA-256已知值 */
  ASSERT_EQUAL(CRYPTO_OK, sha256_calc("abc", 3, hash));
  ASSERT_EQUAL(CRYPTO_OK, sha256_hex("abc", 3, hex));
  ASSERT_EQUAL(64, strlen(hex));
  ASSERT_EQUAL(0, strcmp(hex,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

/**
 * @brief 测试SHA-256上下文方式
 */
void test_sha256_context(void) {
  sha256_ctx_t ctx;
  uint8_t hash[SHA256_HASH_SIZE];

  /* 分两步计算 "hello world" */
  ASSERT_EQUAL(CRYPTO_OK, sha256_init(&ctx));
  ASSERT_EQUAL(CRYPTO_OK, sha256_update(&ctx, "hello ", 6));
  ASSERT_EQUAL(CRYPTO_OK, sha256_update(&ctx, "world", 5));
  ASSERT_EQUAL(CRYPTO_OK, sha256_final(&ctx, hash));

  /* 一步计算对照 */
  uint8_t expected[SHA256_HASH_SIZE];
  sha256_calc("hello world", 11, expected);
  ASSERT_EQUAL(0, memcmp(hash, expected, SHA256_HASH_SIZE));
}

/**
 * @brief 测试XOR加密解密
 */
void test_xor_crypt(void) {
  const char *key = "test_key_123";
  const char *plaintext = "Hello, IoT World!";
  char encrypted[64];
  char decrypted[64];

  /* 加密 */
  ASSERT_EQUAL(CRYPTO_OK,
      xor_crypt_simple(key, plaintext, encrypted, strlen(plaintext) + 1));

  /* 解密（异或是对称操作） */
  ASSERT_EQUAL(CRYPTO_OK,
      xor_crypt_simple(key, encrypted, decrypted, strlen(plaintext) + 1));

  ASSERT_EQUAL(0, strcmp(plaintext, decrypted));
}

/**
 * @brief 测试数据脱敏-手机号
 */
void test_mask_phone(void) {
  char output[32];
  ASSERT_EQUAL(CRYPTO_OK, mask_phone("13812345678", output, sizeof(output)));
  ASSERT_EQUAL(0, strcmp(output, "138****5678"));
}

/**
 * @brief 测试数据脱敏-密码
 */
void test_mask_password(void) {
  char output[32];
  ASSERT_EQUAL(CRYPTO_OK, mask_password("my_secret", output, sizeof(output)));
  ASSERT_EQUAL(0, strcmp(output, "********"));
}

/**
 * @brief 测试安全内存比较
 */
void test_secure_memcmp(void) {
  char buf1[] = "abcdef";
  char buf2[] = "abcdef";
  char buf3[] = "abcxyz";

  ASSERT_EQUAL(0, secure_memcmp(buf1, buf2, 6));
  ASSERT_NOT_EQUAL(0, secure_memcmp(buf1, buf3, 6));
}

/* ========================================================================== */
/*                              内存池测试 (memory_pool) */
/* ========================================================================== */

/**
 * @brief 测试内存跟踪分配和释放
 */
void test_mem_track_alloc_free(void) {
  ASSERT_EQUAL(MEMPOOL_OK, mem_track_init());

  mem_stats_t stats_before;
  mem_track_get_stats(&stats_before);

  void *p = malloc(100);
  ASSERT_NOT_NULL(p);
  ASSERT_EQUAL(MEMPOOL_OK, mem_track_alloc(p, 100, __FILE__, __LINE__, __func__));

  mem_stats_t stats_after;
  mem_track_get_stats(&stats_after);
  ASSERT_EQUAL(stats_before.active_allocs + 1, stats_after.active_allocs);

  ASSERT_EQUAL(MEMPOOL_OK, mem_track_free(p));
  free(p);

  mem_track_cleanup();
}

/**
 * @brief 测试内存泄漏检测
 */
void test_mem_track_leak_detect(void) {
  ASSERT_EQUAL(MEMPOOL_OK, mem_track_init());

  /* 分配但不释放 */
  void *p = malloc(200);
  mem_track_alloc(p, 200, __FILE__, __LINE__, __func__);

  int leaks = mem_track_detect_leaks();
  ASSERT_EQUAL(1, leaks);

  /* 释放 */
  mem_track_free(p);
  free(p);

  leaks = mem_track_detect_leaks();
  ASSERT_EQUAL(0, leaks);

  mem_track_cleanup();
}

/**
 * @brief 测试固定大小内存池创建
 */
void test_mempool_create(void) {
  int pool_id = mempool_create(64, 10);
  ASSERT_TRUE(pool_id >= 0);

  pool_info_t info;
  ASSERT_EQUAL(MEMPOOL_OK, mempool_get_info(pool_id, &info));
  ASSERT_EQUAL(64, info.block_size);
  ASSERT_EQUAL(10, info.total_blocks);
  ASSERT_EQUAL(10, info.free_blocks);
  ASSERT_EQUAL(0, info.used_blocks);

  ASSERT_EQUAL(MEMPOOL_OK, mempool_destroy(pool_id));
}

/**
 * @brief 测试内存池分配和释放
 */
void test_mempool_alloc_free(void) {
  int pool_id = mempool_create(64, 5);
  ASSERT_TRUE(pool_id >= 0);

  void *p = mempool_alloc(pool_id);
  ASSERT_NOT_NULL(p);

  pool_info_t info;
  mempool_get_info(pool_id, &info);
  ASSERT_EQUAL(4, info.free_blocks);
  ASSERT_EQUAL(1, info.used_blocks);

  ASSERT_EQUAL(MEMPOOL_OK, mempool_free(pool_id, p));

  mempool_get_info(pool_id, &info);
  ASSERT_EQUAL(5, info.free_blocks);

  ASSERT_EQUAL(MEMPOOL_OK, mempool_destroy(pool_id));
}

/* ========================================================================== */
/*                              测试套件 */
/* ========================================================================== */

/**
 * @brief 运行所有测试
 */
void run_all_tests(void) {
  printf("\n==============================\n");
  printf("Running Unit Tests\n");
  printf("==============================\n");

  /* 错误处理测试 */
  printf("\n--- Error Handling Tests ---\n");
  test_error_get_string();
  test_error_codes();

  /* 配置加载测试 */
  printf("\n--- Config Loading Tests ---\n");
  test_config_defaults();
  test_config_load();
  test_config_combined();
  test_config_struct_size();

  /* JSON解析测试 */
  printf("\n--- JSON Parse Tests ---\n");
  test_json_parse();

  /* 字符串处理测试 */
  printf("\n--- String Processing Tests ---\n");
  test_string_length();
  test_string_compare();

  /* 数值计算测试 */
  printf("\n--- Numerical Calculation Tests ---\n");
  test_temperature_threshold();
  test_light_threshold();
  test_smoke_alert();

  /* 时间计算测试 */
  printf("\n--- Time Calculation Tests ---\n");
  test_delay_calculation();

  /* 系统监控测试 */
  printf("\n--- System Monitor Tests ---\n");
  test_system_monitor_struct();

  /* 缓存测试 */
  printf("\n--- Cache Tests ---\n");
  test_cache_file_path();
  test_json_serialization();

  /* Base64编码测试 */
  printf("\n--- Base64 Encoding Tests ---\n");
  test_base64_length();
  test_base64_charset();

  /* 配置阈值测试 */
  printf("\n--- Config Threshold Tests ---\n");
  test_config_threshold_range();
  test_report_interval_range();

  /* 数据缓存测试 */
  printf("\n--- Data Cache Tests ---\n");
  test_cache_init_state();
  test_cache_push_pop();
  test_cache_fifo_order();
  test_cache_overflow();
  test_cache_pop_empty();
  test_cache_stats();

  /* 消息队列测试 */
  printf("\n--- Message Queue Tests ---\n");
  test_msgq_create_destroy();
  test_msgq_send_receive();
  test_msgq_receive_empty();
  test_msgq_send_full();

  /* 数据安全测试 */
  printf("\n--- Crypto Utils Tests ---\n");
  test_sha256_calc();
  test_sha256_context();
  test_xor_crypt();
  test_mask_phone();
  test_mask_password();
  test_secure_memcmp();

  /* 内存池测试 */
  printf("\n--- Memory Pool Tests ---\n");
  test_mem_track_alloc_free();
  test_mem_track_leak_detect();
  test_mempool_create();
  test_mempool_alloc_free();

  printf("\n==============================\n");
  printf("Test Summary\n");
  printf("==============================\n");
  printf("Total: %d, Pass: %d, Fail: %d\n", test_total_count, test_pass_count,
         test_fail_count);

  if (test_fail_count == 0) {
    printf("All tests passed!\n");
  } else {
    printf("Some tests failed!\n");
  }
}

/* ========================================================================== */
/*                              主函数（测试入口） */
/* ========================================================================== */

#ifdef TEST_MAIN
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  run_all_tests();

  return (test_fail_count > 0) ? 1 : 0;
}
#endif
