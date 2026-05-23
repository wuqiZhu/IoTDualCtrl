```
物联网双模控制系统

基于 ARM-Linux (I.MX6ULL)的物联网设备控制系统，支持本地 QT 触摸屏控制与远程 MQTT 控制，内置自动化联动引擎，并实现云端数据可视化（InfluxDB + Grafana）。

## 功能特性

- **本地控制**：QT 图形界面，实时显示传感器数据，按钮控制 LED、风扇、照明。
- **远程控制**：通过 MQTT 发送 JSON 指令，支持公网远程操作。
- **自动化联动**（边缘自治）：
  - 温度 > 32℃ 自动开启风扇，温度 < 30℃ 自动关闭。
  - 烟雾传感器（DO）报警时强制开启风扇并推送告警。
  - 光线变暗 + 人体感应 → 自动开灯；无人 30 秒后自动关灯。
- **数据可视化**：传感器数据定期上报至 MQTT 主题，云端 Python 脚本写入 InfluxDB，Grafana 展示实时曲线。

## 硬件接线

| 模块            |      引脚      | 说明 |
|----------------|---------------|------|
| DHT11      	| GPIO0 (115) 	 | 温湿度 |
| PIR  			| GPIO1 (116)   | 人体红外 |
| 烟雾传感器 DO   | GPIO2 (117)   | 数字输出（0=报警，1=正常） |
| 继电器1（风扇） | GPIO3 (118) 	| 控制风扇 |
| 继电器2（LED灯）| GPIO4 (119) 	| 控制照明灯 |
| 光敏传感器 AO	 | ADC_B (ch3) 	| 模拟亮度（0~4095） |

> 风扇和 LED 灯使用独立电池盒（4.5V）供电，所有 GND 必须共地。

## 编译与运行

### 1. 环境准备

- 交叉编译工具链：`arm-buildroot-linux-gnueabihf-gcc/g++`
- 开发板已烧写 Buildroot 系统（含 Qt5 库）
- 依赖库：`libmqttclient.so`、`libjsonrpcc.a`、`libev.a`、`cJSON`

### 2. 设置环境变量（安全配置）

在开发板上运行前，请设置以下环境变量：

​```bash
export MQTT_HOST="your_mqtt_broker_ip"
export MQTT_USER="your_username"
export MQTT_PASS="your_password"
```



### 3. 编译各个模块

bash

```
# 编译 RPC 服务
cd rpc_server
make clean && make

# 编译 MQTT 桥接程序
cd ../mqtt_bridge
make clean && make

# 编译 QT 界面（在 Qt Creator 中打开项目，选择交叉编译套件）
cd ../LED_and_TempHumi
# 在 Qt Creator 中执行 qmake → 构建
```



### 4. 部署到开发板

将生成的可执行文件（`rpc_server`、`mqtt_bridge`、`LED_and_TempHumi`）及驱动模块（`led_drv.ko`、`dht11_drv.ko`）通过 `adb` 或 `scp` 复制到开发板 `/root` 目录。

### 5. 启动系统

bash

```
# 加载驱动
insmod led_drv.ko
insmod dht11_drv.ko

# 启动服务（需先设置 MQTT 环境变量）
export MQTT_HOST="..." MQTT_USER="..." MQTT_PASS="..."
./rpc_server &
./mqtt_bridge &
./LED_and_TempHumi
```



## 云端数据可视化

1. 在云服务器上安装 Mosquitto、InfluxDB、Grafana。
2. 配置 Mosquitto 开启认证（用户名/密码）。
3. 运行 Python 脚本 `cloud/mqtt_to_influxdb.py`（需设置环境变量）。
4. 在 Grafana 中添加 InfluxDB 数据源，导入仪表盘。

### Python 脚本环境变量示例（`.env`）

bash

```
INFLUXDB_URL=http://localhost:8086
INFLUXDB_TOKEN=your_token
INFLUXDB_ORG=your_org
INFLUXDB_BUCKET=your_bucket
MQTT_BROKER=localhost
MQTT_PORT=1883
MQTT_USER=your_mqtt_user
MQTT_PASS=your_mqtt_pass
```



## 远程 MQTT 指令格式

发布主题：`device/control`

| 指令         | JSON 示例                                  |
| :----------- | :----------------------------------------- |
| 控制 LED     | `{"method":"led_control","params":[1]}`    |
| 控制风扇     | `{"method":"relay_control","params":[1]}`  |
| 控制照明     | `{"method":"relay2_control","params":[1]}` |
| 读取温湿度   | `{"method":"dht11_read"}`                  |
| 读取烟雾状态 | `{"method":"smoke_digital_read"}`          |

响应主题：`device/response`，遥测数据主题：`device/telemetry`。

## 常见问题

### 编译时找不到 `mqttclient.h`

请确认头文件路径正确，或设置 `INCLUDE_PATH`。

### `mqtt_bridge` 连接失败

检查环境变量 `MQTT_HOST`、`MQTT_USER`、`MQTT_PASS` 是否正确设置，且网络可访问 Broker。

### QT 界面不显示中文

已将界面文本改为英文，避免编码问题。

## 许可证

本项目使用 [MIT License](https://license/)。

## 致谢

- [Eclipse Paho](https://www.eclipse.org/paho/) (MQTT C 库)
- [cJSON](https://github.com/DaveGamble/cJSON)
- [jsonrpc-c](https://github.com/hmmg/jsonrpc-c)
- [mqttclient](https://github.com/jiejieTop/mqttclient)