/**
 * @file web_api.c
 * @brief Web API处理函数实现
 * @author zhuxiangbo
 * @date 2026-06-04
 * @version 1.1
 *
 * 实现Web管理界面的REST API端点。
 * 使用cJSON进行JSON解析，提高健壮性。
 */
#include "camera_manager.h"
#include "cJSON.h"
#include "hal.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================== */
/*                              API处理函数 */
/* ========================================================================== */

int api_get_sensors(const char *method, const char *path, const char *body,
                    char *response, int response_size) {
  (void)method; (void)path; (void)body;
  int pir = -1, light = -1, relay1 = -1, relay2 = -1, smoke_digital = -1;
  int humidity = -1, temperature = -1;
  hal_sensor_pir_read(&pir);
  hal_sensor_light_read(&light);
  hal_sensor_smoke_digital_read(&smoke_digital);
  hal_sensor_dht11_read(&humidity, &temperature);
  hal_relay1_read(&relay1);
  hal_relay2_read(&relay2);
  snprintf(response, response_size,
           "{\"pir\":%d,\"light\":%d,\"smoke_digital\":%d,"
           "\"temp\":%d,\"humi\":%d,\"relay\":%d,\"relay2\":%d}",
           pir, light, smoke_digital, temperature, humidity, relay1, relay2);
  return 0;
}

int api_get_relay(const char *method, const char *path, const char *body,
                  char *response, int response_size) {
  (void)method; (void)body;
  int relay_id = 0, state = 0; hal_error_t ret;
  if (strstr(path, "/relay/1")) relay_id = 1;
  else if (strstr(path, "/relay/2")) relay_id = 2;
  else { snprintf(response, response_size, "{\"error\":\"Invalid relay ID\"}"); return -1; }
  ret = (relay_id == 1) ? hal_relay1_read(&state) : hal_relay2_read(&state);
  if (ret != HAL_OK) { snprintf(response, response_size, "{\"error\":\"Failed to read relay\"}"); return -1; }
  snprintf(response, response_size, "{\"relay\":%d,\"state\":%d}", relay_id, state);
  return 0;
}

int api_control_relay(const char *method, const char *path, const char *body,
                      char *response, int response_size) {
  (void)method;
  int relay_id = 0, state = 0; hal_error_t ret;
  if (strstr(path, "/relay/1/control")) relay_id = 1;
  else if (strstr(path, "/relay/2/control")) relay_id = 2;
  else { snprintf(response, response_size, "{\"success\":0,\"error\":\"Invalid relay ID\"}"); return -1; }
  if (body && strlen(body) > 0) {
    cJSON *root = cJSON_Parse(body);
    if (root) { cJSON *s = cJSON_GetObjectItem(root, "state"); if (s && cJSON_IsNumber(s)) state = s->valueint; cJSON_Delete(root); }
  }
  ret = (relay_id == 1) ? hal_relay1_control(state) : hal_relay2_control(state);
  if (ret != HAL_OK) { snprintf(response, response_size, "{\"success\":0,\"error\":\"Control failed\"}"); return -1; }
  snprintf(response, response_size, "{\"success\":1,\"relay\":%d,\"state\":%d}", relay_id, state);
  return 0;
}

int api_control_led(const char *method, const char *path, const char *body,
                    char *response, int response_size) {
  (void)method; (void)path;
  int state = 0; hal_error_t ret;
  if (body && strlen(body) > 0) {
    cJSON *root = cJSON_Parse(body);
    if (root) { cJSON *s = cJSON_GetObjectItem(root, "state"); if (s && cJSON_IsNumber(s)) state = s->valueint; cJSON_Delete(root); }
  }
  ret = hal_led_control(state);
  if (ret != HAL_OK) { snprintf(response, response_size, "{\"success\":0,\"error\":\"LED control failed\"}"); return -1; }
  snprintf(response, response_size, "{\"success\":1,\"state\":%d}", state);
  return 0;
}

int api_get_system_status(const char *method, const char *path,
                          const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  FILE *f; double uptime = 0; long total_mem = 0, free_mem = 0;
  f = fopen("/proc/uptime", "r"); if (f) { fscanf(f, "%lf", &uptime); fclose(f); }
  f = fopen("/proc/meminfo", "r");
  if (f) { char line[256]; while (fgets(line, sizeof(line), f)) { sscanf(line, "MemTotal: %ld kB", &total_mem); sscanf(line, "MemFree: %ld kB", &free_mem); } fclose(f); }
  snprintf(response, response_size, "{\"uptime\":%.0f,\"mem_total\":%ld,\"mem_free\":%ld}", uptime, total_mem, free_mem);
  return 0;
}

/* ========================================================================== */
/*                        以下为原有扩展API（重建） */
/* ========================================================================== */

int api_get_sensor_status(const char *method, const char *path,
                          const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  hal_sensor_info_t info; int offset = 0;
  offset += snprintf(response + offset, response_size - offset, "{");
  const char *names[] = {"dht11","pir","light","smoke"};
  hal_sensor_id_t ids[] = {HAL_SENSOR_DHT11, HAL_SENSOR_PIR, HAL_SENSOR_LIGHT, HAL_SENSOR_SMOKE};
  for (int i = 0; i < 4; i++) {
    hal_sensor_get_status(ids[i], &info);
    const char *s = (info.status == HAL_SENSOR_STATUS_ONLINE) ? "online" : (info.status == HAL_SENSOR_STATUS_OFFLINE) ? "offline" : "unknown";
    offset += snprintf(response + offset, response_size - offset, "%s\"%s\":{\"status\":\"%s\",\"failures\":%d}", (i > 0) ? "," : "", names[i], s, info.failure_count);
  }
  snprintf(response + offset, response_size - offset, "}");
  return 0;
}

int api_get_device_info(const char *method, const char *path,
                        const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  struct utsname uts; char hostname[64] = "unknown", kernel[64] = "unknown";
  if (uname(&uts) == 0) { strncpy(hostname, uts.nodename, sizeof(hostname)-1); snprintf(kernel, sizeof(kernel), "%s %s", uts.sysname, uts.release); }
  unsigned long disk_total = 0, disk_free = 0; struct statvfs vfs;
  if (statvfs("/root", &vfs) == 0) { disk_total = (vfs.f_blocks * vfs.f_frsize) / (1024*1024); disk_free = (vfs.f_bavail * vfs.f_frsize) / (1024*1024); }
  FILE *f = fopen("/proc/uptime", "r"); double uptime = 0; if (f) { fscanf(f, "%lf", &uptime); fclose(f); }
  snprintf(response, response_size,
           "{\"hostname\":\"%s\",\"kernel\":\"%s\",\"uptime\":%.0f,\"disk_total_mb\":%lu,\"disk_free_mb\":%lu}",
           hostname, kernel, uptime, disk_total, disk_free);
  return 0;
}

/* ===== 摄像头抓拍 ===== */
int api_camera_capture(const char *method, const char *path,
                       const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  int need_cleanup = 0;
  if (!camera_get_status()) {
    if (camera_init(NULL) != 0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"camera init failed\"}"); return 0; }
    need_cleanup = 1;
  }
  char filepath[128]; time_t now = time(NULL);
  snprintf(filepath, sizeof(filepath), "www/camera_capture.jpg");
  if (camera_capture_jpeg(filepath) != 0) {
    if (need_cleanup) camera_cleanup();
    snprintf(response, response_size, "{\"success\":0,\"error\":\"capture failed\"}"); return 0;
  }
  FILE *fp = fopen(filepath, "rb"); long size = 0;
  if (fp) { fseek(fp, 0, SEEK_END); size = ftell(fp); fclose(fp); }
  if (need_cleanup) camera_cleanup();
  snprintf(response, response_size, "{\"success\":1,\"size\":%ld,\"file\":\"camera_capture.jpg\",\"timestamp\":%ld}", size, now);
  return 0;
}

int api_camera_list(const char *method, const char *path,
                    const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  int offset = 0, has_entry = 0;
  offset += snprintf(response + offset, response_size - offset, "{\"files\":[");
  FILE *fp = fopen("www/camera_capture.jpg", "r");
  if (fp) { fclose(fp); offset += snprintf(response + offset, response_size - offset, "\"camera_capture.jpg\""); has_entry = 1; }
  fp = popen("ls -t /tmp/camera_*.jpg 2>/dev/null | head -10", "r");
  if (fp) { char line[256]; while (fgets(line, sizeof(line), fp) && offset < response_size - 50) { line[strcspn(line,"\n")]=0; if (strlen(line)>0) { offset += snprintf(response+offset, response_size-offset, "%s\"%s\"", has_entry?",":"", line); has_entry=1; } } pclose(fp); }
  snprintf(response + offset, response_size - offset, "]}");
  return 0;
}

int api_camera_view(const char *method, const char *path,
                    const char *body, char *response, int response_size) {
  (void)method; (void)body;
  char filepath[256] = {0}; const char *q;
  if ((q = strstr(path, "file="))) { q += 5; const char *end = strchr(q, '&'); if (end) { size_t len = (size_t)(end - q); if (len < sizeof(filepath)) { memcpy(filepath, q, len); } } else { strncpy(filepath, q, sizeof(filepath)-1); } }
  if (strlen(filepath) == 0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"missing file param\"}"); return 0; }
  int allowed = 0;
  if (strcmp(filepath, "camera_capture.jpg") == 0) allowed = 1;
  else if (strncmp(filepath, "/tmp/camera_", 12) == 0) allowed = 1;
  if (!allowed) { snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid path\"}"); return 0; }
  FILE *fp = fopen(filepath, "rb");
  if (!fp && strchr(filepath, '/') == NULL) { char www[256]; snprintf(www, sizeof(www), "www/%s", filepath); fp = fopen(www, "rb"); }
  if (!fp) { snprintf(response, response_size, "{\"success\":0,\"error\":\"file not found\"}"); return 0; }
  fseek(fp, 0, SEEK_END); long size = ftell(fp); fseek(fp, 0, SEEK_SET);
  unsigned char *img = (unsigned char *)malloc(size); if (!img) { fclose(fp); snprintf(response, response_size, "{\"success\":0,\"error\":\"memory\"}"); return 0; }
  fread(img, 1, size, fp); fclose(fp);
  int out_len = 4 * ((size + 2) / 3); char *b64 = (char *)malloc(out_len + 1);
  if (!b64) { free(img); snprintf(response, response_size, "{\"success\":0,\"error\":\"memory\"}"); return 0; }
  camera_encode_base64(img, size, b64, out_len + 1); free(img);
  snprintf(response, response_size, "{\"success\":1,\"image\":\"data:image/jpeg;base64,%s\",\"size\":%ld}", b64, size);
  free(b64); return 0;
}

/* ===== OTA升级 ===== */
int api_ota_upgrade(const char *method, const char *path,
                    const char *body, char *response, int response_size) {
  (void)method; (void)path;
  if (!body || strlen(body) == 0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"empty body\"}"); return -1; }
  cJSON *root = cJSON_Parse(body);
  if (!root) { snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid JSON\"}"); return -1; }
  cJSON *url_item = cJSON_GetObjectItem(root, "url");
  if (!url_item || !cJSON_IsString(url_item)) { cJSON_Delete(root); snprintf(response, response_size, "{\"success\":0,\"error\":\"missing url\"}"); return -1; }
  FILE *fp = fopen("/tmp/ota_command.json", "w");
  if (fp) { fprintf(fp, "{\"method\":\"ota_update\",\"params\":{\"url\":\"%s\"}}", url_item->valuestring); fclose(fp); snprintf(response, response_size, "{\"success\":1,\"message\":\"OTA command sent\",\"url\":\"%s\"}", url_item->valuestring); }
  else { snprintf(response, response_size, "{\"success\":0,\"error\":\"write failed\"}"); }
  cJSON_Delete(root); return 0;
}

int api_ota_status(const char *method, const char *path,
                   const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  FILE *fp = fopen("/tmp/ota_status.json", "r");
  if (fp) { char buf[512]; size_t len = fread(buf, 1, sizeof(buf)-1, fp); fclose(fp); if (len > 0) { buf[len]='\0'; snprintf(response, response_size, "%s", buf); return 0; } }
  snprintf(response, response_size, "{\"state\":\"idle\",\"current_version\":\"3.0.0\",\"target_version\":\"--\",\"progress\":0,\"message\":\"No upgrade\"}");
  return 0;
}

int api_ota_rollback(const char *method, const char *path,
                     const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  FILE *fp = fopen("/tmp/ota_command.json", "w");
  if (fp) { fprintf(fp, "{\"method\":\"ota_rollback\"}"); fclose(fp); snprintf(response, response_size, "{\"success\":1,\"message\":\"Rollback sent\"}"); }
  else { snprintf(response, response_size, "{\"success\":0,\"error\":\"write failed\"}"); }
  return 0;
}

/* ===== 配置管理 ===== */
int api_config_get(const char *method, const char *path,
                   const char *body, char *response, int response_size) {
  (void)method; (void)path; (void)body;
  FILE *fp = fopen("/etc/device/config.json", "r");
  if (!fp) fp = fopen("/tmp/device_config.json", "r");
  if (fp) { char buf[1024]; size_t len = fread(buf, 1, sizeof(buf)-1, fp); fclose(fp); if (len > 0) { buf[len]='\0'; snprintf(response, response_size, "%s", buf); return 0; } }
  snprintf(response, response_size, "{\"temp_high\":32,\"temp_low\":30,\"smoke_fan_duration\":30,\"smoke_alert_interval\":10,\"temp_change\":1,\"humi_change\":5,\"full_report\":300,\"heartbeat\":60}");
  return 0;
}

int api_config_update(const char *method, const char *path,
                      const char *body, char *response, int response_size) {
  (void)method; (void)path;
  if (!body || strlen(body)==0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"empty\"}"); return -1; }
  cJSON *root = cJSON_Parse(body);
  if (!root) { snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid JSON\"}"); return -1; }
  FILE *fp = fopen("/tmp/config_update.json", "w");
  if (fp) { char *s = cJSON_PrintUnformatted(root); if (s) { fprintf(fp, "%s", s); free(s); } fclose(fp); snprintf(response, response_size, "{\"success\":1,\"message\":\"Config update sent\"}"); }
  else { snprintf(response, response_size, "{\"success\":0,\"error\":\"write failed\"}"); }
  cJSON_Delete(root); return 0;
}

/* ===== 日志查看 ===== */
int api_get_logs(const char *method, const char *path,
                 const char *body, char *response, int response_size) {
  (void)method; (void)body;
  char level[16] = "all", filter[64] = "";
  const char *query = strchr(path, '?');
  if (query) { query++; const char *p; if ((p = strstr(query, "level="))) { p += 6; int i=0; while (*p && *p!='&' && i<15) level[i++]=*p++; } if ((p = strstr(query, "filter="))) { p += 7; int i=0; while (*p && *p!='&' && i<63) filter[i++]=*p++; } }
  FILE *fp = fopen("/var/log/app.log", "r");
  if (!fp) fp = fopen("/tmp/app.log", "r");
  int offset = 0; offset += snprintf(response+offset, response_size-offset, "{\"logs\":[");
  if (fp) { char line[512]; int first=1, count=0; while (fgets(line, sizeof(line), fp) && offset < response_size-100 && count < 100) { if (strcmp(level,"all")!=0) { char up[16]; snprintf(up, sizeof(up), "%s", level); for(int i=0;up[i];i++) up[i]=toupper(up[i]); if(!strstr(line, up)) continue; } if(strlen(filter)>0 && !strstr(line,filter)) continue; line[strcspn(line,"\n")]=0; if(strlen(line)>0) { char esc[1024]; int j=0; for(int i=0;line[i]&&j<(int)sizeof(esc)-2;i++) { if(line[i]=='"'||line[i]=='\\') esc[j++]='\\'; esc[j++]=line[i]; } esc[j]='\0'; offset += snprintf(response+offset, response_size-offset, "%s\"%s\"", first?"":"", esc); first=0; count++; } } fclose(fp); }
  snprintf(response+offset, response_size-offset, "]}");
  return 0;
}

/* ========================================================================== */
/*                              SHA-256 工具函数 */
/* ========================================================================== */

/** SHA-256 上下文 */
typedef struct {
  uint32_t state[8];
  uint32_t count[2];
  uint8_t buffer[64];
} sha256_ctx;

static const uint32_t sha256_k[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
  0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
  0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
  0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
  0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
  0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
  0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
  0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
  0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32-(n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x)>>3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x)>>10))

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64]) {
  uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)data[4*i]<<24) | ((uint32_t)data[4*i+1]<<16) |
           ((uint32_t)data[4*i+2]<<8) | data[4*i+3];
  for (int i = 16; i < 64; i++)
    w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
  a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
  e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
  for (int i = 0; i < 64; i++) {
    t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
    t2 = EP0(a) + MAJ(a,b,c);
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
  ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
  ctx->count[0] = ctx->count[1] = 0;
}

static void sha256_update(sha256_ctx *ctx, const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t idx = (ctx->count[0] >> 3) & 0x3f;
  ctx->count[0] += (uint32_t)(len << 3);
  if (ctx->count[0] < (uint32_t)(len << 3)) ctx->count[1]++;
  ctx->count[1] += (uint32_t)(len >> 29);
  uint32_t free = 64 - idx;
  if (len >= free) {
    memcpy(ctx->buffer + idx, bytes, free);
    sha256_transform(ctx, ctx->buffer);
    for (size_t i = free; i + 63 < len; i += 64)
      sha256_transform(ctx, bytes + i);
    idx = 0;
  } else {
    memcpy(ctx->buffer + idx, bytes, len);
    return;
  }
  memcpy(ctx->buffer + idx, bytes + (len - (len & 63)), len & 63);
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
  uint32_t idx = (ctx->count[0] >> 3) & 0x3f;
  ctx->buffer[idx++] = 0x80;
  if (idx > 56) {
    memset(ctx->buffer + idx, 0, 64 - idx);
    sha256_transform(ctx, ctx->buffer);
    idx = 0;
  }
  memset(ctx->buffer + idx, 0, 56 - idx);
  ctx->buffer[63] = ctx->count[0] & 0xff;
  ctx->buffer[62] = (ctx->count[0] >> 8) & 0xff;
  ctx->buffer[61] = (ctx->count[0] >> 16) & 0xff;
  ctx->buffer[60] = (ctx->count[0] >> 24) & 0xff;
  ctx->buffer[59] = ctx->count[1] & 0xff;
  ctx->buffer[58] = (ctx->count[1] >> 8) & 0xff;
  ctx->buffer[57] = (ctx->count[1] >> 16) & 0xff;
  ctx->buffer[56] = (ctx->count[1] >> 24) & 0xff;
  sha256_transform(ctx, ctx->buffer);
  for (int i = 0; i < 8; i++) {
    hash[4*i]   = (ctx->state[i] >> 24) & 0xff;
    hash[4*i+1] = (ctx->state[i] >> 16) & 0xff;
    hash[4*i+2] = (ctx->state[i] >> 8) & 0xff;
    hash[4*i+3] = ctx->state[i] & 0xff;
  }
}

static void sha256_hex(const void *data, size_t len, char hex_out[65]) {
  sha256_ctx ctx;
  uint8_t hash[32];
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, hash);
  for (int i = 0; i < 32; i++)
    sprintf(hex_out + i*2, "%02x", hash[i]);
  hex_out[64] = '\0';
}

/* ===== Web认证 ===== */

/* 从 /dev/urandom 生成安全的随机 token */
static void generate_secure_token(char *token, int token_size) {
  FILE *urandom = fopen("/dev/urandom", "r");
  if (!urandom) {
    /* 应急回退：仍比 time(NULL) 好，因为混合了多种来源 */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned)(ts.tv_nsec ^ (uintptr_t)token));
    for (int i = 0; i < token_size - 1; i++)
      token[i] = "0123456789abcdef"[rand() % 16];
    token[token_size - 1] = '\0';
    return;
  }
  unsigned char raw[16];
  size_t read_len = fread(raw, 1, sizeof(raw), urandom);
  fclose(urandom);
  if (read_len < sizeof(raw)) {
    /* 读不够就用 time 补 */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    raw[0] ^= (unsigned char)(ts.tv_nsec & 0xff);
  }
  int len = token_size - 1;
  if (len > 32) len = 32;
  for (int i = 0; i < len; i++)
    token[i] = "0123456789abcdef"[raw[i % 16] & 0x0f];
  token[len] = '\0';
}

static void hash_password_hex(const char *password, char hex_out[65]) {
  /* 加盐：混合设备标识，即使两个设备密码相同，hash也不同 */
  char salted[256];
  struct utsname uts;
  int off = snprintf(salted, sizeof(salted), "%s", password);
  if (uname(&uts) == 0)
    off += snprintf(salted + off, sizeof(salted) - off, ":%s:%s", uts.nodename, uts.machine);
  sha256_hex(salted, strlen(salted), hex_out);
}

static int verify_auth_token(const char *token) {
  if (!token || strlen(token) == 0) return 0;
  FILE *fp = fopen("/tmp/web_sessions.json", "r");
  if (!fp) return 0;
  char buf[1024]; size_t len = fread(buf, 1, sizeof(buf)-1, fp); fclose(fp);
  if (len == 0) return 0; buf[len] = '\0';
  cJSON *root = cJSON_Parse(buf); if (!root) return 0;
  cJSON *sessions = cJSON_GetObjectItem(root, "sessions"); if (!sessions) { cJSON_Delete(root); return 0; }
  long now = time(NULL); int valid = 0;
  for (int i = 0; i < cJSON_GetArraySize(sessions); i++) {
    cJSON *item = cJSON_GetArrayItem(sessions, i);
    cJSON *t = cJSON_GetObjectItem(item, "token"), *exp = cJSON_GetObjectItem(item, "expires");
    if (t && exp && cJSON_IsString(t) && cJSON_IsNumber(exp) && strcmp(t->valuestring, token) == 0 && exp->valuedouble > now) { valid = 1; break; }
  }
  cJSON_Delete(root); return valid;
}

static void save_session(const char *token, const char *username) {
  cJSON *root = NULL, *sessions = NULL;
  FILE *fp = fopen("/tmp/web_sessions.json", "r");
  if (fp) { char buf[2048]; size_t len = fread(buf, 1, sizeof(buf)-1, fp); fclose(fp); if (len > 0) { buf[len]='\0'; root = cJSON_Parse(buf); } }
  if (!root) root = cJSON_CreateObject();
  sessions = cJSON_GetObjectItem(root, "sessions");
  if (!sessions) { sessions = cJSON_CreateArray(); cJSON_AddItemToObject(root, "sessions", sessions); }
  long now = time(NULL); int i = 0;
  while (i < cJSON_GetArraySize(sessions)) { cJSON *item = cJSON_GetArrayItem(sessions, i); cJSON *exp = cJSON_GetObjectItem(item, "expires"); if (exp && cJSON_IsNumber(exp) && exp->valuedouble <= now) cJSON_DeleteItemFromArray(sessions, i); else i++; }
  cJSON *ns = cJSON_CreateObject();
  cJSON_AddStringToObject(ns, "token", token); cJSON_AddStringToObject(ns, "username", username);
  cJSON_AddNumberToObject(ns, "expires", (double)(now + 86400));
  cJSON_AddItemToArray(sessions, ns);
  char *json_str = cJSON_PrintUnformatted(root); cJSON_Delete(root);
  if (json_str) { fp = fopen("/tmp/web_sessions.json", "w"); if (fp) { fprintf(fp, "%s", json_str); fclose(fp); } free(json_str); }
}

static void extract_token(const char *body, char *token, int token_size) {
  token[0] = '\0'; if (!body) return;
  const char *cookie = strstr(body, "Cookie:");
  if (cookie) { const char *t = strstr(cookie, "auth_token="); if (t) { t += 11; int i=0; while (*t && *t!=';' && *t!=' ' && *t!='\r' && *t!='\n' && i<token_size-1) token[i++] = *t++; token[i]='\0'; return; } }
  if (body[0] == '{') { cJSON *root = cJSON_Parse(body); if (root) { cJSON *t = cJSON_GetObjectItem(root, "token"); if (t && cJSON_IsString(t)) strncpy(token, t->valuestring, token_size-1); cJSON_Delete(root); } }
}

int api_login(const char *method, const char *path,
              const char *body, char *response, int response_size) {
  (void)method; (void)path;
  if (!body || strlen(body)==0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"empty\"}"); return -1; }
  cJSON *root = cJSON_Parse(body); if (!root) { snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid JSON\"}"); return -1; }
  cJSON *user_item = cJSON_GetObjectItem(root, "username"), *pass_item = cJSON_GetObjectItem(root, "password");
  if (!user_item || !pass_item || !cJSON_IsString(user_item) || !cJSON_IsString(pass_item)) { cJSON_Delete(root); snprintf(response, response_size, "{\"success\":0,\"error\":\"missing credentials\"}"); return -1; }

  /* 从配置文件读取密码哈希 */
  char cfg_user[32] = "admin", cfg_hash[65] = "";
  FILE *fp = fopen("/etc/device/web_auth.json", "r");
  if (!fp) fp = fopen("/tmp/web_auth.json", "r");
  if (fp) {
    char buf[512]; size_t len = fread(buf,1,sizeof(buf)-1,fp); fclose(fp);
    if (len>0) { buf[len]='\0'; cJSON *a = cJSON_Parse(buf);
      if (a) { cJSON *u=cJSON_GetObjectItem(a,"username"); cJSON *h=cJSON_GetObjectItem(a,"password_hash");
        if(u&&cJSON_IsString(u)) strncpy(cfg_user,u->valuestring,sizeof(cfg_user)-1);
        if(h&&cJSON_IsString(h)) strncpy(cfg_hash,h->valuestring,sizeof(cfg_hash)-1);
      cJSON_Delete(a); }
    }
  }

  /* 首次启动：无配置文件，生成随机密码 */
  if (strlen(cfg_hash) == 0) {
    /* 生成随机密码 */
    char random_pass[17];
    generate_secure_token(random_pass, sizeof(random_pass));
    hash_password_hex(random_pass, cfg_hash);
    /* 存到 /tmp/web_auth.json */
    cJSON *ao = cJSON_CreateObject();
    cJSON_AddStringToObject(ao, "username", "admin");
    cJSON_AddStringToObject(ao, "password_hash", cfg_hash);
    char *js = cJSON_PrintUnformatted(ao); cJSON_Delete(ao);
    if (js) {
      FILE *wf = fopen("/tmp/web_auth.json", "w");
      if (wf) { fprintf(wf, "%s", js); fclose(wf); chmod("/tmp/web_auth.json", 0600); }
      free(js);
    }
    printf("========================================\n");
    printf("  *** 初始 Web 登录密码: %s ***\n", random_pass);
    printf("  请登录后立即修改密码！\n");
    printf("========================================\n");
  }

  /* 验证：计算输入密码的哈希，与存储值比较 */
  char input_hash[65];
  hash_password_hex(pass_item->valuestring, input_hash);
  if (strcmp(user_item->valuestring, cfg_user) != 0 || strcmp(input_hash, cfg_hash) != 0) {
    cJSON_Delete(root);
    snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid credentials\"}");
    return -1;
  }
  char token[64]; generate_secure_token(token, sizeof(token)); save_session(token, user_item->valuestring);
  cJSON_Delete(root);
  snprintf(response, response_size, "{\"success\":1,\"token\":\"%s\",\"username\":\"%s\",\"expires_in\":86400}", token, cfg_user);
  return 0;
}

int api_logout(const char *method, const char *path,
               const char *body, char *response, int response_size) {
  (void)method; (void)path;
  char token[64] = ""; extract_token(body, token, sizeof(token));
  if (strlen(token) > 0) {
    FILE *fp = fopen("/tmp/web_sessions.json", "r");
    if (fp) { char buf[2048]; size_t len = fread(buf,1,sizeof(buf)-1,fp); fclose(fp); if (len>0) { buf[len]='\0'; cJSON *root=cJSON_Parse(buf); if(root) { cJSON *s=cJSON_GetObjectItem(root,"sessions"); if(s) { int i=0; while(i<cJSON_GetArraySize(s)) { cJSON *item=cJSON_GetArrayItem(s,i); cJSON *t=cJSON_GetObjectItem(item,"token"); if(t&&cJSON_IsString(t)&&strcmp(t->valuestring,token)==0) cJSON_DeleteItemFromArray(s,i); else i++; } } char *js=cJSON_PrintUnformatted(root); cJSON_Delete(root); if(js) { fp=fopen("/tmp/web_sessions.json","w"); if(fp) { fprintf(fp,"%s",js); fclose(fp); } free(js); } } } }
  }
  snprintf(response, response_size, "{\"success\":1,\"message\":\"Logged out\"}");
  return 0;
}

int api_auth_check(const char *method, const char *path,
                   const char *body, char *response, int response_size) {
  (void)method; (void)path;
  char token[64] = ""; extract_token(body, token, sizeof(token));
  if (strlen(token) == 0 && body) { const char *t = strstr(body, "token="); if (t) { t+=6; int i=0; while(*t && *t!='&' && i<63) token[i++]=*t++; } }
  snprintf(response, response_size, "{\"authenticated\":%d}", (strlen(token)>0 && verify_auth_token(token)) ? 1 : 0);
  return 0;
}

int api_change_password(const char *method, const char *path,
                        const char *body, char *response, int response_size) {
  (void)method; (void)path;
  if (!body || strlen(body)==0) { snprintf(response, response_size, "{\"success\":0,\"error\":\"empty\"}"); return -1; }
  cJSON *root = cJSON_Parse(body); if (!root) { snprintf(response, response_size, "{\"success\":0,\"error\":\"invalid JSON\"}"); return -1; }
  cJSON *old_pass = cJSON_GetObjectItem(root, "old_password"), *new_pass = cJSON_GetObjectItem(root, "new_password");
  if (!old_pass || !new_pass || !cJSON_IsString(old_pass) || !cJSON_IsString(new_pass)) { cJSON_Delete(root); snprintf(response, response_size, "{\"success\":0,\"error\":\"missing fields\"}"); return -1; }
  if (strlen(new_pass->valuestring) < 6) { cJSON_Delete(root); snprintf(response, response_size, "{\"success\":0,\"error\":\"password too short (min 6)\"}"); return -1; }

  /* 读取存储的密码哈希 */
  char stored_hash[65] = "";
  FILE *fp = fopen("/etc/device/web_auth.json", "r");
  if (!fp) fp = fopen("/tmp/web_auth.json", "r");
  if (fp) { char buf[512]; size_t len = fread(buf,1,sizeof(buf)-1,fp); fclose(fp);
    if (len>0) { buf[len]='\0'; cJSON *a=cJSON_Parse(buf);
      if(a) { cJSON *h=cJSON_GetObjectItem(a,"password_hash");
        if(h&&cJSON_IsString(h)) strncpy(stored_hash,h->valuestring,sizeof(stored_hash)-1);
      cJSON_Delete(a); }
    }
  }

  /* 验证旧密码 */
  char old_hash[65];
  hash_password_hex(old_pass->valuestring, old_hash);
  if (strlen(stored_hash) == 0 || strcmp(old_hash, stored_hash) != 0) {
    cJSON_Delete(root);
    snprintf(response, response_size, "{\"success\":0,\"error\":\"wrong password\"}");
    return -1;
  }

  /* 存新密码的哈希 */
  char new_hash[65];
  hash_password_hex(new_pass->valuestring, new_hash);
  cJSON *auth_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(auth_obj, "username", "admin");
  cJSON_AddStringToObject(auth_obj, "password_hash", new_hash);
  char *json_str = cJSON_PrintUnformatted(auth_obj);
  cJSON_Delete(auth_obj); cJSON_Delete(root);

  if (json_str) {
    fp = fopen("/tmp/web_auth.json", "w");
    if (fp) { fprintf(fp, "%s", json_str); fclose(fp); chmod("/tmp/web_auth.json", 0600);
      snprintf(response, response_size, "{\"success\":1,\"message\":\"Password changed\"}");
    } else { snprintf(response, response_size, "{\"success\":0,\"error\":\"save failed\"}"); }
    free(json_str);
  }
  return 0;
}

/* ===== 数据导出 ===== */
int api_export_data(const char *method, const char *path,
                    const char *body, char *response, int response_size) {
  (void)method; (void)body;
  char format[16] = "json";
  const char *query = strchr(path, '?');
  if (query) { const char *fmt = strstr(query, "format="); if (fmt) { fmt += 7; int i=0; while (*fmt && *fmt!='&' && i<15) format[i++]=*fmt++; } }
  int temp = -1, humi = -1, pir = -1, light = -1, smoke = -1, relay1 = -1, relay2 = -1;
  hal_sensor_dht11_read(&humi, &temp); hal_sensor_pir_read(&pir); hal_sensor_light_read(&light);
  hal_sensor_smoke_digital_read(&smoke); hal_relay1_read(&relay1); hal_relay2_read(&relay2);
  char hostname[64] = ""; FILE *fp = popen("hostname", "r");
  if (fp) { if (fgets(hostname, sizeof(hostname), fp)) hostname[strcspn(hostname,"\n")]=0; pclose(fp); }
  long timestamp = time(NULL); char time_str[64]; struct tm *tm_info = localtime(&timestamp); strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
  if (strcmp(format, "csv") == 0) { int off = 0; off += snprintf(response+off, response_size-off, "timestamp,device,temperature,humidity,pir,light,smoke,relay1,relay2\r\n"); off += snprintf(response+off, response_size-off, "%s,%s,%d,%d,%d,%d,%d,%d,%d\r\n", time_str, hostname, temp, humi, pir, light, smoke, relay1, relay2); }
  else { snprintf(response, response_size, "{\"success\":1,\"format\":\"json\",\"data\":{\"timestamp\":\"%s\",\"device\":\"%s\",\"temperature\":%d,\"humidity\":%d,\"pir\":%d,\"light\":%d,\"smoke\":%d,\"relay1\":%d,\"relay2\":%d}}", time_str, hostname, temp, humi, pir, light, smoke, relay1, relay2); }
  return 0;
}
