#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "cJSON.h"
#include "rpc.h"
#include "rpc_client.h"

static int g_iSocketClient = -1;
static pthread_mutex_t rpc_mutex = PTHREAD_MUTEX_INITIALIZER;

int RPC_Client_Init(void);

static int read_with_timeout(int sock, char *buf, int buf_size, int timeout_ms)
{
    fd_set read_fds;
    struct timeval tv;
    int ret;

    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(sock + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
        printf("select error: %s\n", strerror(errno));
        return -1;
    }
    if (ret == 0) {
        printf("read timeout after %d ms\n", timeout_ms);
        return 0;
    }

    return read(sock, buf, buf_size - 1);
}

static int safe_send_locked(const char *buf, int len)
{
    int ret;
    int sock = g_iSocketClient;

    if (sock <= 0) {
        if (RPC_Client_Init() < 0) {
            return -1;
        }
        sock = g_iSocketClient;
    }

    ret = send(sock, buf, len, 0);
    if (ret <= 0 && (errno == EPIPE || errno == ECONNRESET)) {
        printf("Connection broken, reconnecting...\n");
        if (RPC_Client_Init() < 0) {
            return -1;
        }
        sock = g_iSocketClient;
        ret = send(sock, buf, len, 0);
    }

    return ret;
}

static int read_response_and_parse_locked(int *result)
{
    char buf[300];
    int iLen;
    int sock = g_iSocketClient;
    int max_retries = 10;
    int retry_count = 0;

    do {
        iLen = read_with_timeout(sock, buf, sizeof(buf), 3000);
        if (iLen < 0) {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
        if (iLen == 0) {
            retry_count++;
            if (retry_count >= max_retries) {
                printf("read timeout after %d retries\n", max_retries);
                return -1;
            }
            continue;
        }
        buf[iLen] = 0;
    } while (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'));

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        printf("JSON parse error\n");
        return -1;
    }
    cJSON *result_obj = cJSON_GetObjectItem(root, "result");
    if (result_obj && cJSON_IsNumber(result_obj)) {
        *result = result_obj->valueint;
        cJSON_Delete(root);
        return 0;
    }
    cJSON_Delete(root);
    return -1;
}

static int read_response_and_parse_array_locked(int *humi, int *temp)
{
    char buf[300];
    int iLen;
    int sock = g_iSocketClient;
    int max_retries = 10;
    int retry_count = 0;

    do {
        iLen = read_with_timeout(sock, buf, sizeof(buf), 3000);
        if (iLen < 0) {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
        if (iLen == 0) {
            retry_count++;
            if (retry_count >= max_retries) {
                printf("read timeout after %d retries\n", max_retries);
                return -1;
            }
            continue;
        }
        buf[iLen] = 0;
    } while (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'));

    cJSON *root = cJSON_Parse(buf);
    if (!root) return -1;
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result && cJSON_IsArray(result)) {
        cJSON *a = cJSON_GetArrayItem(result, 0);
        cJSON *b = cJSON_GetArrayItem(result, 1);
        if (a && b) {
            *humi = a->valueint;
            *temp = b->valueint;
            cJSON_Delete(root);
            return 0;
        }
    }
    cJSON_Delete(root);
    return -1;
}

static int rpc_call_int_result(const char *method, const char *params, int *result)
{
    char buf[200];
    int ret;
    int local_result;

    sprintf(buf, "{\"method\": \"%s\", \"params\": [%s], \"id\": \"1\" }", method, params);

    pthread_mutex_lock(&rpc_mutex);

    ret = safe_send_locked(buf, strlen(buf));
    if (ret != (int)strlen(buf)) {
        printf("send rpc request err : %d, %s\n", ret, strerror(errno));
        pthread_mutex_unlock(&rpc_mutex);
        return -1;
    }

    ret = read_response_and_parse_locked(&local_result);
    pthread_mutex_unlock(&rpc_mutex);

    if (ret == 0) {
        *result = local_result;
        return 0;
    }
    return -1;
}

static int rpc_call_no_result(const char *method, const char *params)
{
    int result;
    return rpc_call_int_result(method, params, &result);
}

int rpc_led_control(int on)
{
    char params[16];
    sprintf(params, "%d", on);
    return rpc_call_no_result("led_control", params);
}

int rpc_dht11_read(char *humi, char *temp)
{
    char buf[200];
    int ret;
    int h, t;

    sprintf(buf, "{\"method\": \"dht11_read\", \"params\": [0], \"id\": \"2\" }");

    pthread_mutex_lock(&rpc_mutex);

    ret = safe_send_locked(buf, strlen(buf));
    if (ret != (int)strlen(buf)) {
        printf("send rpc request err : %d, %s\n", ret, strerror(errno));
        pthread_mutex_unlock(&rpc_mutex);
        return -1;
    }

    ret = read_response_and_parse_array_locked(&h, &t);
    pthread_mutex_unlock(&rpc_mutex);

    if (ret == 0) {
        *humi = (char)h;
        *temp = (char)t;
        return 0;
    }
    return -1;
}

int rpc_pir_read(int *value)
{
    return rpc_call_int_result("pir_read", "", value);
}

int rpc_light_read(int *value)
{
    return rpc_call_int_result("light_read", "", value);
}

int rpc_relay_control(int on)
{
    char params[16];
    sprintf(params, "%d", on);
    return rpc_call_no_result("relay_control", params);
}

int rpc_relay_read(int *value)
{
    return rpc_call_int_result("relay_read", "", value);
}

int rpc_smoke_digital_read(int *value)
{
    return rpc_call_int_result("smoke_digital_read", "", value);
}

int rpc_relay2_control(int on)
{
    char params[16];
    sprintf(params, "%d", on);
    return rpc_call_no_result("relay2_control", params);
}

int rpc_relay2_read(int *value)
{
    return rpc_call_int_result("relay2_read", "", value);
}

int RPC_Client_Init(void)
{
    int iSocketClient;
    struct sockaddr_in tSocketServerAddr;
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

    tSocketServerAddr.sin_family = AF_INET;
    tSocketServerAddr.sin_port = htons(PORT);
    inet_aton("127.0.0.1", &tSocketServerAddr.sin_addr);
    memset(tSocketServerAddr.sin_zero, 0, 8);

    iRet = connect(iSocketClient, (const struct sockaddr *)&tSocketServerAddr, sizeof(struct sockaddr));
    if (-1 == iRet) {
        printf("connect error!\n");
        close(iSocketClient);
        return -1;
    }

    g_iSocketClient = iSocketClient;
    printf("RPC_Client_Init connected successfully\n");
    return iSocketClient;
}
