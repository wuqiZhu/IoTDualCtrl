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

// ========== ԭ�е� RPC ���� ==========
int rpc_led_control(int on)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"led_control\", \"params\": [%d], \"id\": \"2\" }", on);
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen ==  strlen(buf))
    {
        while (1) 
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        } 
        
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) {
                printf("rpc_led_control: failed to parse JSON response\n");
                return -1;
            }
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                ret = result->valueint;
            }
            else
            {
                printf("rpc_led_control: result is NULL or not a number\n");
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

int rpc_dht11_read(char *humi, char *temp)
{
    char buf[300];
    int iLen;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"dht11_read\"," \
                   "\"params\": [0], \"id\": \"2\" }");        
            
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen ==  strlen(buf))
    {
        while (1) 
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        } 
        
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result)
            {
                cJSON * a = cJSON_GetArrayItem(result,0);
                cJSON * b = cJSON_GetArrayItem(result,1);
                *humi = a->valueint;
                *temp = b->valueint;
                cJSON_Delete(root);
                return 0;
            }
            else
            {
                cJSON_Delete(root);
                return -1;
            }
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

int rpc_pir_read(int *value)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"pir_read\", \"params\": [], \"id\": \"3\" }");
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen == strlen(buf))
    {
        while (1)
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        }
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                *value = result->valueint;
                ret = 0;
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

int rpc_light_read(int *value)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"light_read\", \"params\": [], \"id\": \"4\" }");
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen == strlen(buf))
    {
        while (1)
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        }
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                *value = result->valueint;
                ret = 0;
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

int rpc_relay_control(int on)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"relay_control\", \"params\": [%d], \"id\": \"5\" }", on);
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen == strlen(buf))
    {
        while (1)
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        }
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                ret = result->valueint;  // 0 ��ʾ�ɹ�
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

// ========== �������̵���2��LED�ƣ����� ==========
int rpc_relay2_control(int on)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"relay2_control\", \"params\": [%d], \"id\": \"9\" }", on);
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen == strlen(buf))
    {
        while (1)
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        }
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                ret = result->valueint;
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

// ========== ���������������źŶ�ȡ ==========
int rpc_smoke_digital_read(int *value)
{
    char buf[100];
    int iLen;
    int ret = -1;
    int iSocketClient = g_iSocketClient;

    sprintf(buf, "{\"method\": \"smoke_digital_read\", \"params\": [], \"id\": \"11\" }");
    iLen = send(iSocketClient, buf, strlen(buf), 0);
    if (iLen == strlen(buf))
    {
        while (1)
        {
            iLen = read(iSocketClient, buf, sizeof(buf));
            buf[iLen] = 0;
            if (iLen == 1 && (buf[0] == '\r' || buf[0] == '\n'))
                continue;
            else
                break;
        }
        if (iLen > 0)
        {
            cJSON *root = cJSON_Parse(buf);
            cJSON *result = cJSON_GetObjectItem(root, "result");
            if (result && cJSON_IsNumber(result))
            {
                *value = result->valueint;
                ret = 0;
            }
            cJSON_Delete(root);
            return ret;
        }
        else
        {
            printf("read rpc reply err : %d\n", iLen);
            return -1;
        }
    }
    else
    {
        printf("send rpc request err : %d, %s\n", iLen, strerror(errno));
        return -1;
    }
}

// ========== RPC �ͻ��˳�ʼ�� ==========
int RPC_Client_Init(void) 
{
    int iSocketClient;
    struct sockaddr_in tSocketServerAddr;
    int iRet;

    iSocketClient = socket(AF_INET, SOCK_STREAM, 0);
    if (iSocketClient < 0) {
        printf("socket error\n");
        return -1;
    }

    tSocketServerAddr.sin_family      = AF_INET;
    tSocketServerAddr.sin_port        = htons(PORT);
    inet_aton("127.0.0.1", &tSocketServerAddr.sin_addr);
    memset(tSocketServerAddr.sin_zero, 0, 8);

    iRet = connect(iSocketClient, (const struct sockaddr *)&tSocketServerAddr, sizeof(struct sockaddr));	
    if (-1 == iRet)
    {
        printf("connect error!\n");
        close(iSocketClient);
        return -1;
    }

    g_iSocketClient = iSocketClient;
    printf("RPC client connected to rpc_server\n");
    return iSocketClient;    
}

// ========== �����а��� ==========
static void print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s led <0|1>          : control LED (0 off, 1 on)\n", prog);
    printf("  %s dht11              : read DHT11 temperature and humidity\n", prog);
    printf("  %s pir                : read PIR motion sensor\n", prog);
    printf("  %s light              : read light sensor (0 bright, 1 dark)\n", prog);
    printf("  %s relay <0|1>        : control relay1 (fan) (0 off, 1 on)\n", prog);
    printf("  %s relay2 <0|1>       : control relay2 (LED lamp) (0 off, 1 on)\n", prog);
    printf("  %s smoke_digital      : read smoke sensor digital value (0=alert, 1=normal)\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    // ��ʼ�� RPC ����
    if (RPC_Client_Init() < 0) {
        printf("Failed to initialize RPC client\n");
        return -1;
    }

    if (strcmp(argv[1], "led") == 0) {
        if (argc != 3) {
            printf("Error: missing parameter for led\n");
            return -1;
        }
        int on = atoi(argv[2]);
        int ret = rpc_led_control(on);
        printf("LED control %s, ret = %d\n", on ? "ON" : "OFF", ret);
    }
    else if (strcmp(argv[1], "dht11") == 0) {
        char humi, temp;
        int ret = rpc_dht11_read(&humi, &temp);
        if (ret == 0) {
            printf("DHT11: Humidity = %d%%, Temperature = %d��C\n", humi, temp);
        } else {
            printf("DHT11 read failed\n");
        }
    }
    else if (strcmp(argv[1], "pir") == 0) {
        int value;
        int ret = rpc_pir_read(&value);
        if (ret == 0) {
            printf("PIR: %s\n", value ? "Motion detected" : "No motion");
        } else {
            printf("PIR read failed\n");
        }
    }
    else if (strcmp(argv[1], "light") == 0) {
        int value;
        int ret = rpc_light_read(&value);
        if (ret == 0) {
            printf("Light: %s (value=%d)\n", value ? "Dark" : "Bright", value);
        } else {
            printf("Light read failed\n");
        }
    }
    else if (strcmp(argv[1], "relay") == 0) {
        if (argc != 3) {
            printf("Error: missing parameter for relay\n");
            return -1;
        }
        int on = atoi(argv[2]);
        int ret = rpc_relay_control(on);
        printf("Relay1 (fan) %s, ret = %d\n", on ? "ON" : "OFF", ret);
    }
    else if (strcmp(argv[1], "relay2") == 0) {
        if (argc != 3) {
            printf("Error: missing parameter for relay2\n");
            return -1;
        }
        int on = atoi(argv[2]);
        int ret = rpc_relay2_control(on);
        printf("Relay2 (LED lamp) %s, ret = %d\n", on ? "ON" : "OFF", ret);
    }
    else if (strcmp(argv[1], "smoke_digital") == 0) {
        int value;
        int ret = rpc_smoke_digital_read(&value);
        if (ret == 0) {
            printf("Smoke digital: %s (value=%d)\n", value ? "Normal" : "Alert", value);
        } else {
            printf("Smoke digital read failed\n");
        }
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}
