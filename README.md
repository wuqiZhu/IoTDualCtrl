# 🏠 智能物联网环境监控系统

> **基于 NXP i.MX6ULL ARM Cortex-A7 的嵌入式物联网全栈项目**  
> 五层架构 · 7×24 稳定运行 · 从驱动写到云端

<div align="center">

[![C](https://img.shields.io/badge/language-C/C++-00599C?style=flat-square&logo=c)](https://github.com/wuqiZhu/IoTDualCtrl)
[![Platform](https://img.shields.io/badge/platform-ARM_Linux-5391FE?style=flat-square&logo=arm)](https://github.com/wuqiZhu/IoTDualCtrl)
[![MQTT](https://img.shields.io/badge/protocol-MQTT-660066?style=flat-square&logo=mqtt)](https://github.com/wuqiZhu/IoTDualCtrl)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)

</div>

---

## 📋 项目简介

本项目是一个完整的嵌入式物联网智能环境监控系统，基于 **NXP i.MX6ULL** 平台，采用 **五层分层架构**：

```
┌──────────────────────────────────────┐
│         云端层 (InfluxDB/Grafana)      │
├──────────────────────────────────────┤
│      应用客户端层 (Web/MQTT/RPC)        │
├──────────────────────────────────────┤
│        RPC服务层 (JSON-RPC over TCP)   │
├──────────────────────────────────────┤
│      HAL硬件抽象层 (接口统一封装)        │
├──────────────────────────────────────┤
│        内核驱动层 (GPIO/ADC/V4L2)      │
└──────────────────────────────────────┘
         i.MX6ULL ARM Cortex-A7
```

**核心能力：**
- 🔌 接入 **6+ 种传感器/执行器**（温湿度、烟雾、人体红外、光敏、继电器、LED）
- 📷 **USB 摄像头** V4L2 驱动，烟雾报警自动抓拍上传钉钉
- 🌐 **Web 管理界面**（HTTP 8080），15+ REST API 端点
- ☁️ **MQTT 云端**上报，InfluxDB + Grafana 可视化
- 🤖 **智能边缘控制**：3 级优先级自动联动（烟雾 > 温度 > 光照+PIR）
- 🔒 **安全体系**：设备认证、安全审计、数据脱敏、OTA 升级
- ⏱ **7×24 小时**稳定运行，断网缓存自动重传

---

## 🖼️ 系统架构图

```
硬件外设 → 内核驱动 → HAL抽象层 → RPC服务(TCP:1234)
                                   ├──→ HTTP服务器(Web:8080)
                                   ├──→ MQTT Bridge → 阿里云
                                   │                  ├─ InfluxDB + Grafana
                                   │                  └─ 钉钉告警(含图片)
                                   └──→ RPC Client(命令行/Qt)
```

---

## 🔧 硬件清单

| 外设 | 接口 | 说明 |
|------|------|------|
| LED (板载) | GPIO131 | /dev/100ask_led |
| DHT11 温湿度 | GPIO115 | 单总线协议，中断+定时器解析 |
| PIR 人体红外 | GPIO116 | 数字输入 |
| 烟雾传感器 DO | GPIO117 | 数字输入 |
| 继电器1 (风扇) | GPIO118 | 数字输出 |
| 继电器2 (LED灯) | GPIO119 | 数字输出 |
| 光敏电阻 ADC | ADC 通道3 | /sys/bus/iio |
| USB 摄像头 | /dev/video1 | V4L2 MJPEG 640×480 |

---

## 🚀 快速开始

### 硬件要求
- i.MX6ULL 开发板
- 按上方清单接入传感器
- USB 摄像头

### 编译与部署

```bash
# 1. 编译共享库
cd shared_lib && make clean && make && cd ..

# 2. 编译 RPC Server
cd lesson5/rpc_server && make clean && make && cd ../..

# 3. 编译 MQTT Bridge
cd lesson6 && make clean && make && cd ..

# 4. 部署到开发板
# 将 rpc_server 和 mqtt_bridge 复制到 /usr/local/bin/
# 将 config.json 复制到 /root/

# 5. 启动服务
systemctl start rpc_server
systemctl start mqtt_bridge

# 6. 访问 Web 界面
# http://开发板IP:8080
```

### 云端部署

```bash
# 在阿里云 ECS 上
cd /opt/iot_mqtt_to_influx
nohup python3 -u mqtt_to_influxdb.py > mqtt_to_influxdb.log 2>&1 &

# 需要 MQTT Broker + InfluxDB + Grafana (Docker Compose 一键部署)
cd grafana && docker-compose up -d
```

---

## 📂 项目结构

```
.
├── lesson5/
│   ├── rpc_server/          # RPC 服务端 + HTTP Web 服务器
│   │   ├── hal.c/h          # 硬件抽象层（核心设计）
│   │   ├── rpc_server.c      # JSON-RPC 服务，注册9个方法
│   │   ├── http_server.c/h   # 轻量级 HTTP 服务器
│   │   ├── web_api.c         # Web API（传感器/控制/摄像头/配置）
│   │   ├── camera_manager.c/h# V4L2 摄像头管理
│   │   └── www/              # Web 前端界面
│   ├── rpc_client/           # 命令行 RPC 客户端
│   └── jsonrpc-c/ libev/     # 静态编译库
├── lesson6/                  # MQTT 智能网关
│   ├── mqtt_bridge.cpp       # MQTT 桥接主程序（核心）
│   ├── rpc_client.cpp/h      # RPC 客户端库
│   ├── data_cache.c/h        # 环形缓冲区缓存（断网重传）
│   ├── ota_manager.c/h       # OTA 远程升级+回滚
│   ├── device_auth.c/h       # 设备认证
│   ├── security_audit.c/h    # 安全审计
│   ├── crypto_utils.c/h      # 数据加密+SHA-256
│   ├── memory_pool.c/h       # 内存池
│   ├── msg_queue.c/h         # 消息队列
│   ├── sensor_manager.c/h    # 传感器管理
│   ├── camera_manager.c/h    # 摄像头管理
│   ├── plugin_manager.c/h    # 插件管理器
│   ├── perf_monitor.c/h      # 性能监控
│   ├── system_monitor.c/h    # 系统监控
│   └── test_cases.c          # 265个单元测试
├── cloud/
│   └── mqtt_to_influxdb.py   # MQTT → InfluxDB 桥接
├── grafana/                  # Docker 部署（InfluxDB+Grafana+Telegraf）
├── shared_lib/               # 公共库（cJSON + watchdog）
├── config.json               # 系统配置
├── check_all.sh              # 一键检查脚本
└── deploy.sh                 # 部署脚本
```

---

## ✨ 核心特性

| 特性 | 说明 |
|------|------|
| **五层架构** | 驱动 → HAL → RPC → 客户端 → 云端，层间解耦 |
| **HAL 抽象层** | 封装所有硬件操作，换平台只需重写 hal.c |
| **双通道通信** | JSON-RPC（本地）+ MQTT（远程）+ HTTP REST（Web） |
| **摄像头** | V4L2 MJPEG 驱动，首帧丢弃防黑屏，双通道上传 |
| **边缘控制** | 3级优先级、滞后控制、故障自愈 |
| **高可用** | 环形缓冲区缓存100条、OTA升级+回滚、看门狗 |
| **安全** | Token+SHA-256认证、IP锁定、数据脱敏 |
| **可扩展** | 插件管理器(dlopen)、消息队列、内存池 |
| **代码质量** | 265个单元测试、static analysis、valgrind |
| **DevOps** | 一键部署脚本、快照管理、编译检查 |

---

## 📊 测试覆盖

```bash
# 运行单元测试（x86_64）
cd lesson6 && gcc -DTEST_MAIN -o test test_cases.c error.c config.c \
  data_cache.c msg_queue.c crypto_utils.c memory_pool.c \
  -I../shared_lib/include ../shared_lib/src/cJSON.c \
  -lm -lpthread -I. -lcrypto && ./test

# 输出示例
# Total: 265, Pass: 265, Fail: 0
# All tests passed!
```

---

## 🙏 致谢与第三方组件

本项目参考或使用了以下开源项目的代码：

| 组件 | 许可协议 | 版权信息 |
|------|---------|---------|
| [libev](http://software.schmorp.de/pkg/libev.html) | BSD-like | Copyright (c) 2007-2013 Marc Alexander Lehmann |
| [jsonrpc-c](https://github.com/hungys/jsonrpc-c) | MIT | Copyright (c) 2012-2013 Henrique Gomes |
| [cJSON](https://github.com/DaveGamble/cJSON) | MIT | Copyright (c) 2009-2017 Dave Gamble and cJSON contributors |
| [mqttclient](https://github.com/jiejieTop/mqttclient) | Apache 2.0 | Copyright (c) 2019-2022 jiejie (jiejieTop) |
| [mbedtls](https://github.com/Mbed-TLS/mbedtls) | Apache 2.0 | Copyright (c) 2006-2018, Arm Limited |
| SHA-256 (crypto_utils) | 公有领域 | 参考 Brad Conte 的公有域实现 |
| DHT11 驱动 | — | 标准单总线协议实现（嵌入式通用知识） |
| V4L2 摄像头驱动 | — | Linux V4L2 框架标准 API 使用 |

**本项目原创代码部分（hal、mqtt_bridge、rpc_server、http_server、data_cache、ota_manager、security_audit 等）基于 MIT 协议开源。**

---

## 📜 协议

本项目基于 MIT 协议开源。详见 [LICENSE](LICENSE) 文件。

---

## 👤 作者

**朱相波** · 长春大学旅游学院 物联网工程  
[GitHub](https://github.com/wuqiZhu/IoTDualCtrl) · 求职：嵌入式软件/Linux 应用开发实习生

---

如果这个项目对你有帮助，请点一个 ⭐，谢谢！
