#ifndef _RPC_CLIENT_H
#define _RPC_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

int rpc_led_control(int on);
int rpc_dht11_read(char *humi, char *temp);
int rpc_pir_read(int *value);
int rpc_light_read(int *value);
int rpc_relay_control(int on);
int rpc_relay_read(int *value);
int rpc_smoke_digital_read(int *value);
int rpc_relay2_control(int on);
int rpc_relay2_read(int *value);
int RPC_Client_Init(void);

#ifdef __cplusplus
}
#endif

#endif