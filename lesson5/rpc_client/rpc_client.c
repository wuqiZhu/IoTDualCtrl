#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "cJSON.h"
#include "rpc.h"

static int g_iSocketClient;
static char g_server_ip[16] = "127.0.0.1";

/**
 * @brief RPC通用调用：发送JSON-RPC请求并解析整数结果
 * @param method 方法名
 * @param params 参数字符串（不含方括号）
 * @param result 输出结果
 * @return 0成功, -1失败
 */
static int rpc_call_int(const char *method, const char *params, int *result)
{
    char buf[300];
    int iLen;
    int ret = -1;
    int sock = g_iSocketClient;

    snprintf(buf, sizeof(buf), "{\"method\": \"%s\", \"params\": [%s], \"id\": \"1\" }",
             method, params ? params : "");
    iLen = send(sock, buf, strlen(buf), 0);
    if (iLen != (int)strlen(buf)) {
        printf("send %s req err: %s\n", method, strerror(errno));
        return -1;
    }

    while (1) {
        iLen = read(sock, buf, sizeof(buf) - 1);
        if (iLen <= 0) {
            printf("read %s reply err: %d\n", method, iLen);
            return -1;
        }
        buf[iLen] = '\0';
        if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
            continue;
        break;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) return -1;
    cJSON *r = cJSON_GetObjectItem(root, "result");
    if (r && cJSON_IsNumber(r)) {
        if (result) *result = r->valueint;
        ret = 0;
    }
    cJSON_Delete(root);
    return ret;
}

int rpc_led_control(int on)
{
    char p[16];
    snprintf(p, sizeof(p), "%d", on);
    return rpc_call_int("led_control", p, NULL);
}

int rpc_dht11_read(char *humi, char *temp)
{
    char buf[300];
    int iLen;
    int sock = g_iSocketClient;

    const char *req = "{\"method\": \"dht11_read\", \"params\": [0], \"id\": \"2\" }";
    iLen = send(sock, req, strlen(req), 0);
    if (iLen != (int)strlen(req)) {
        printf("send dht11 req err: %s\n", strerror(errno));
        return -1;
    }

    while (1) {
        iLen = read(sock, buf, sizeof(buf) - 1);
        if (iLen <= 0) return -1;
        buf[iLen] = '\0';
        if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n')) continue;
        break;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result && cJSON_IsArray(result)) {
        cJSON *a = cJSON_GetArrayItem(result, 0);
        cJSON *b = cJSON_GetArrayItem(result, 1);
        if (a && b) {
            *humi = (char)a->valueint;
            *temp = (char)b->valueint;
            cJSON_Delete(root);
            return 0;
        }
    }
    cJSON_Delete(root);
    return -1;
}

int rpc_pir_read(int *value)
{
    return rpc_call_int("pir_read", "", value);
}

int rpc_light_read(int *value)
{
    return rpc_call_int("light_read", "", value);
}

int rpc_relay_control(int on)
{
    char p[16];
    snprintf(p, sizeof(p), "%d", on);
    return rpc_call_int("relay_control", p, NULL);
}

int rpc_relay_read(int *value)
{
    return rpc_call_int("relay_read", "", value);
}

int rpc_relay2_control(int on)
{
    char p[16];
    snprintf(p, sizeof(p), "%d", on);
    return rpc_call_int("relay2_control", p, NULL);
}

int rpc_relay2_read(int *value)
{
    return rpc_call_int("relay2_read", "", value);
}

int rpc_smoke_digital_read(int *value)
{
    return rpc_call_int("smoke_digital_read", "", value);
}

int rpc_camera_capture_jpeg(const char *filename)
{
    char p[200];
    snprintf(p, sizeof(p), "\"%s\"", filename ? filename : "/tmp/capture.jpg");
    return rpc_call_int("camera_capture", p, NULL);
}

int RPC_Client_Init(void)
{
    int iSocketClient;
    struct sockaddr_in addr;
    int iRet;

    if (g_iSocketClient > 0) {
        close(g_iSocketClient);
        g_iSocketClient = -1;
    }

    iSocketClient = socket(AF_INET, SOCK_STREAM, 0);
    if (iSocketClient < 0) {
        printf("socket error\n");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_aton(g_server_ip, &addr.sin_addr);
    memset(addr.sin_zero, 0, 8);

    iRet = connect(iSocketClient, (const struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (-1 == iRet) {
        printf("connect to %s:%d failed: %s\n", g_server_ip, PORT, strerror(errno));
        close(iSocketClient);
        return -1;
    }

    g_iSocketClient = iSocketClient;
    printf("RPC client connected to %s:%d\n", g_server_ip, PORT);
    return iSocketClient;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] <command> [params]\n", prog);
    printf("Options:\n");
    printf("  -s <ip>    RPC server IP address (default: 127.0.0.1)\n");
    printf("Commands:\n");
    printf("  led <0|1>          Control LED (0 off, 1 on)\n");
    printf("  dht11              Read DHT11 temperature and humidity\n");
    printf("  pir                Read PIR motion sensor\n");
    printf("  light              Read light sensor (0 bright, 1 dark)\n");
    printf("  relay <0|1>        Control relay1 (fan) (0 off, 1 on)\n");
    printf("  relay_read         Read relay1 (fan) state\n");
    printf("  relay2 <0|1>       Control relay2 (LED lamp) (0 off, 1 on)\n");
    printf("  relay2_read        Read relay2 (LED lamp) state\n");
    printf("  smoke_digital      Read smoke sensor (0=alert, 1=normal)\n");
    printf("  camera_capture     Capture JPEG image via RPC\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    /* Parse -s server_ip option */
    int optind = 1;
    if (argc > 2 && strcmp(argv[1], "-s") == 0) {
        if (argc < 4) {
            printf("Error: -s requires an IP argument\n");
            return -1;
        }
        strncpy(g_server_ip, argv[2], sizeof(g_server_ip) - 1);
        optind = 3;
    }

    if (RPC_Client_Init() < 0) {
        printf("Failed to initialize RPC client\n");
        return -1;
    }

    const char *cmd = argv[optind];

    if (strcmp(cmd, "led") == 0) {
        if (argc <= optind + 1) { printf("Error: missing param\n"); return -1; }
        int on = atoi(argv[optind + 1]);
        printf("LED %s, ret=%d\n", on ? "ON" : "OFF", rpc_led_control(on));
    }
    else if (strcmp(cmd, "dht11") == 0) {
        char humi, temp;
        if (rpc_dht11_read(&humi, &temp) == 0)
            printf("DHT11: Humidity=%d%%, Temperature=%dC\n", humi, temp);
        else
            printf("DHT11 read failed\n");
    }
    else if (strcmp(cmd, "pir") == 0) {
        int v;
        if (rpc_pir_read(&v) == 0) printf("PIR: %s\n", v ? "Motion" : "None");
        else printf("PIR read failed\n");
    }
    else if (strcmp(cmd, "light") == 0) {
        int v;
        if (rpc_light_read(&v) == 0) printf("Light: %s\n", v ? "Dark" : "Bright");
        else printf("Light read failed\n");
    }
    else if (strcmp(cmd, "relay") == 0) {
        if (argc <= optind + 1) { printf("Error: missing param\n"); return -1; }
        int on = atoi(argv[optind + 1]);
        printf("Relay1(fan) %s, ret=%d\n", on ? "ON" : "OFF", rpc_relay_control(on));
    }
    else if (strcmp(cmd, "relay_read") == 0) {
        int v;
        if (rpc_relay_read(&v) == 0) printf("Relay1(fan): %s\n", v ? "ON" : "OFF");
        else printf("relay_read failed\n");
    }
    else if (strcmp(cmd, "relay2") == 0) {
        if (argc <= optind + 1) { printf("Error: missing param\n"); return -1; }
        int on = atoi(argv[optind + 1]);
        printf("Relay2(LED) %s, ret=%d\n", on ? "ON" : "OFF", rpc_relay2_control(on));
    }
    else if (strcmp(cmd, "relay2_read") == 0) {
        int v;
        if (rpc_relay2_read(&v) == 0) printf("Relay2(LED): %s\n", v ? "ON" : "OFF");
        else printf("relay2_read failed\n");
    }
    else if (strcmp(cmd, "smoke_digital") == 0) {
        int v;
        if (rpc_smoke_digital_read(&v) == 0) printf("Smoke: %s\n", v ? "Normal" : "ALERT");
        else printf("smoke read failed\n");
    }
    else if (strcmp(cmd, "camera_capture") == 0) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/rpc_capture_%ld.jpg", time(NULL));
        if (rpc_camera_capture_jpeg(path) == 0)
            printf("Camera captured: %s\n", path);
        else
            printf("Camera capture failed\n");
    }
    else {
        printf("Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}
