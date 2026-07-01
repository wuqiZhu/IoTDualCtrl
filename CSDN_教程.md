---
title: i.MX6ULL AI火灾检测系统 — 从零搭建教程（开源）
date: 2026-06-30
categories: 嵌入式开发
tags: [i.MX6ULL, YOLOv8, 火灾检测, 教程, 开源, 嵌入式AI]
---

## 一、你需要准备什么

### 硬件

| 硬件 | 数量 | 备注 |
|------|------|------|
| i.MX6ULL 开发板 | 1块 | 百问网100ask_imx6ull_pro |
| USB 摄像头 | 1个 | /dev/video1，UVC免驱 |
| DHT11 温湿度 | 1个 | GPIO115 |
| PIR 人体红外 | 1个 | GPIO116 |
| 烟雾传感器 MQ-2 | 1个 | GPIO117 |
| 继电器模块 | 2个 | GPIO118(风扇)/GPIO119(LED灯) |
| 光敏电阻 | 1个 | ADC通道3 |
| 7寸LCD屏（可选） | 1个 | 触摸屏显示Qt界面 |

### 软件

| 软件 | 用途 |
|------|------|
| Ubuntu 虚拟机 | 交叉编译环境 |
| ARM交叉工具链 | arm-buildroot-linux-gnueabihf-gcc 7.5.0 |
| AutoDL 账号（可选） | GPU云训练模型 |
| 腾讯云服务器 | AI推理服务 |
| MQTT Broker | mosquitto |
| InfluxDB + Grafana | 数据存储和可视化 |

---

## 二、硬件接线

| 传感器 | 开发板引脚 |
|--------|-----------|
| DHT11 数据 | GPIO115 (J5-13) |
| PIR 输出 | GPIO116 (J5-15) |
| 烟雾传感器 DO | GPIO117 (J5-17) |
| 继电器1 IN | GPIO118 (J5-19) |
| 继电器2 IN | GPIO119 (J5-21) |
| 光敏电阻 ADC | LDR-GND (ADC通道3) |
| USB摄像头 | USB HOST口 |

---

## 三、下载源码与编译

### 3.1 获取源码

```bash
git clone https://github.com/wuqiZhu/IoTDualCtrl.git
cd IoTDualCtrl
```

### 3.2 编译共享库

```bash
cd shared_lib && make clean && make
cd ..
```

### 3.3 编译RPC Server

```bash
cd lesson5/rpc_server && make clean && make
cd ../..
```

### 3.4 编译MQTT Bridge

```bash
cd lesson6 && make clean && make
cd ..
```

---

## 四、配置

### 4.1 修改配置文件

编辑 `config.json`，修改这几项：

```json
{
    "ai_detection": {
        "server_host": "your_ai_server_ip",   ← 改成你的腾讯云IP
        "server_port": 5081,
        "enabled": 1
    }
}
```

### 4.2 设置环境变量

创建 `/root/.env`：

```bash
export MQTT_HOST=your_mqtt_broker_ip
export MQTT_PORT=1883
export MQTT_USERNAME=your_username
export MQTT_PASSWORD=your_password
export MQTT_CLIENTID=mqtt_bridge
```

---

## 五、部署到开发板

### 5.1 传输文件

```bash
# 传到开发板
scp lesson5/rpc_server/rpc_server root@<板子IP>:/usr/local/bin/
scp lesson6/mqtt_bridge root@<板子IP>:/usr/local/bin/
scp config.json root@<板子IP>:/root/
```

### 5.2 加载驱动

```bash
insmod led_drv.ko
insmod dht11_drv.ko
```

### 5.3 启动RPC Server

```bash
./rpc_server &
```

### 5.4 启动MQTT Bridge

```bash
source /root/.env
./mqtt_bridge &
```

---

## 六、部署云端推理服务

### 6.1 下载模型

从 [GitHub Releases](https://github.com/wuqiZhu/IoTDualCtrl/releases) 下载 `fire_model.onnx`，或自己训练：

```bash
# 方法一：下载预训练模型
wget https://github.com/wuqiZhu/IoTDualCtrl/releases/download/v1.0/fire_model.onnx

# 方法二：自己训练（需要GPU）
from ultralytics import YOLO
model = YOLO('yolov8n.pt')
model.train(data='data.yaml', epochs=100, imgsz=640, batch=16)
model.export(format='onnx')
```

### 6.2 部署推理服务

```bash
# 上传到腾讯云
scp fire_model.onnx ubuntu@your_server_ip:/data/models/
scp cloud/fire_server.py ubuntu@your_server_ip:/data/models/

# 安装依赖
pip3 install flask onnxruntime numpy pillow

# 启动服务
cd /data/models && nohup python3 -u fire_server.py &
```

### 6.3 配置systemd（可选）

```ini
[Unit]
Description=AI Fire Detection Server
After=network.target

[Service]
Type=simple
User=ubuntu
WorkingDirectory=/data/models
ExecStart=/usr/bin/python3 /data/models/fire_server.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### 6.4 验证推理服务

```bash
curl -s http://your_server_ip:5081/health
# 返回: {"status":"ok"}

curl -s -X POST http://your_server_ip:5081/detect \
  -F "image=@test.jpg"
# 返回: {"fire_detected":false, "smoke_detected":true, ...}
```

---

## 七、部署云端数据服务

### 7.1 启动MQTT Broker

```bash
# Ubuntu
sudo apt-get install -y mosquitto
sudo systemctl start mosquitto
```

### 7.2 启动InfluxDB + Grafana（Docker）

```bash
cd grafana
docker-compose up -d

# 访问：http://your_server_ip:3000
# 默认账号：admin / admin123
```

### 7.3 启动数据桥接

```bash
# 设置环境变量
export INFLUXDB_TOKEN="your_influxdb_token"
export MQTT_PASS="your_mqtt_password"
export DINGTALK_WEBHOOK="your_dingtalk_webhook"

# 启动
cd /opt/iot_mqtt_to_influx
nohup python3 -u mqtt_to_influxdb.py > mqtt_to_influxdb.log 2>&1 &
```

---

## 八、测试

### 8.1 检查系统状态

```bash
# Web管理界面
curl http://<板子IP>:8080/api/sensors

# 应该返回：
# {"pir":0,"light":0,"smoke_digital":1,"temp":28,"humi":55,...}
```

### 8.2 测试AI检测

```bash
# 用打火机靠近摄像头，看日志
# 应该出现：
AI detect: fire=1 smoke=0 alert=1 max_conf=0.46
AI vote: THRESHOLD MET! Triggering alert.
Alert published: {"method":"smoke_alert"...}
```

### 8.3 测试烟雾传感器

```bash
# 打火机靠近烟雾传感器，看日志
# 应该出现：
Event: Smoke changed 1 -> 0
Fusion: smoke sensor +40
Auto: MONITOR → ALERT (score=xx)
```

### 8.4 查看Grafana

```
浏览器打开 http://your_server_ip:3000
默认仪表板显示所有传感器数据和AI检测结果
```

---

## 九、常见问题

### Q: AI推理一直返回 HTTP request failed

检查腾讯云防火墙是否放行了5081端口：

```bash
sudo ufw status
# 或者腾讯云控制台 → 安全组 → 放行5081
```

### Q: 钉钉收不到通知

检查环境变量：

```bash
echo $DINGTALK_WEBHOOK
# 确认Webhook地址正确
```

### Q: RPC连接频繁超时

检查rpc_server是否在运行，以及config.json中的超时设置：

```json
"rpc": {
    "read_timeout_ms": 5000,
}
```

---

## 十、下一步可以做什么

这个系统已经是一个完整的AIoT闭环，但还有很多可以扩展的方向：

1. **换带NPU的板子**（RK3588等），实现本地推理
2. **多摄像头**，覆盖仓库多个角度
3. **RTSP视频流**，实时推流到云端
4. **数据闭环**，误报反馈 → 自动优化模型
5. **告警升级**，短信/电话通知

---

项目地址：https://github.com/wuqiZhu/IoTDualCtrl

如果对你有帮助，GitHub 点个 Star ⭐
