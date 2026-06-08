# CURRENT.md — 项目变更记录

## 2026-06-07 开发记录

### 一、云端脚本更新 (`cloud/mqtt_to_influxdb.py`)

**背景**：`.bak1` 是阿里云上运行的旧版脚本，配置硬编码，功能不完整。

**变更**：
- 合并 `.bak`（环境变量模式） 和 `.bak1`（真实凭据） 两个版本
- 凭据作为环境变量默认值嵌入，同时支持环境变量覆盖
- **新增 `device/heartbeat` 订阅** — 接收心跳指标写入 InfluxDB `device_heartbeat` 测量
- **新增 `handle_image_uploaded()`** — 处理 HTTP 图片上传成功通知
- **新增 `handle_error_report()`** — 错误报告写入 InfluxDB，HIGH/CRITICAL 钉钉通知
- **新增 `handle_heartbeat()`** — 解析心跳数据（cpu_usage、memory、load_avg、uptime）
- **新增 `handle_response()` 完善** — 跳过 system_status/sensor_status 等状态查询响应
- **新增 `on_disconnect` 回调** — 记录 MQTT 意外断开
- 统一日志格式（`logging` 替代 `print`）

**部署方式不变**：
```bash
cd /opt/iot_mqtt_to_influx && nohup python3 -u mqtt_to_influxdb.py > mqtt_to_influxdb.log 2>&1 &
```

### 二、开发板环境变量配置 (`.env`)

**背景**：`mqtt_bridge.service` 通过 `EnvironmentFile=-/root/.env` 加载配置。

**变更**：
- 在项目根目录创建 `.env`，填入阿里云服务器真实凭据
- 包含：`MQTT_HOST=8.140.232.52`、`MQTT_USERNAME=zhuxiangbo`、`MQTT_PASSWORD=13979831637Zhu@` 等
- 使用 `chmod 600` 保护凭据

### 三、测试文档 (`操作.md`)

创建完整测试操作指南，覆盖：环境检查、编译、单元测试、板端部署、传感器验证、MQTT控制、Web管理、日志查看、硬件调试。

### 四、风扇继电器跳匝问题修复 (`lesson6/mqtt_bridge.cpp`)

**问题现象**：温度超过32°C时风扇能正常吹，但每5~30秒继电器会跳匝一次（咔嗒声）。

**根因一：`smoke_fan_until` 初始值为 0**
```
auto_control_smoke: if (fan_state && now >= *smoke_fan_until)
                   → now(17亿) >= 0 永远为真！
                   无烟雾时也每5秒关一次风扇
auto_control_temp:  temp > 32 → 又打开
                   继电器每5秒跳一次
```

**根因二：烟雾定时器到期后与温度控制打架**
```
烟雾报警 → smoke_fan_until = now + 30
30秒后到期 → auto_control_smoke关风扇
          → auto_control_temp: temp>32 → 又开
          单次跳匝。且 smoke_fan_until 未重置，每5秒重复跳
```

**修复方案**（`auto_control_smoke` else 分支）：
1. **`*smoke_fan_until > 0` 守卫** — 没发生过烟雾告警就不介入
2. **温度检查** — 定时器到期时检查 `g_sensor_cache.temp < temp_high_threshold`，温度仍高就不关风扇，交给温度联动管理
3. **重置 `*smoke_fan_until = 0`** — 处理完后重置，防止下次循环重复触发

**修复后行为**：
- 温度 > 32°C → 风扇一直吹，**继电器不跳**
- 烟雾报警 → 风扇开，定时器30秒
- 定时器到期时温度仍 > 32 → **不关风扇**，保持吹
- 温度降到 < 30°C → 温度联动关风扇

### 五、check_all.sh 已知问题

1. `grep "/sys" rpc_server.c` 会误命中路径 `/api/system`
2. 硬编码 IP `8.140.232.52` 检查是合理硬编码（阿里云固定地址）
3. 单元测试编译命令过期：cJSON 已移入 shared_lib

---

## 2026-06-08 开发记录

### 一、摄像头架构修复：统一由 rpc_server 管理

**背景**：`/dev/video1` 只能被一个进程打开，mqtt_bridge 和 rpc_server 争抢摄像头导致 Web 抓拍失败、烟雾拍照失败。

**根因**：mqtt_bridge 初始化时 `camera_init()` 占住 `/dev/video1`，Web 抓拍时 `api_camera_capture` 无法初始化摄像头。

**变更**：
- **rpc_server 新增 `camera_capture_jpeg` RPC 方法**：临时 init→capture→cleanup，用完即释放
- **rpc_client 新增 `rpc_camera_capture_jpeg()`**：通过 RPC 调用 rpc_server 拍照
- **mqtt_bridge 移除所有直接摄像头操作**：
  - 删除 camera_init() / camera_cleanup() 调用
  - 所有 camera_capture_jpeg() → rpc_camera_capture_jpeg()
  - 所有 camera_capture_base64() → 本地文件读取 + 本地 base64 编码
- **从 lesson6/Makefile 移除 camera_manager.c**（不再编译）
- **lesson6/camera_manager.c/h 保留在仓库中**（不编译，板子上有备份）

**涉及文件**：`rpc_server.c`、`rpc_client.h`、`rpc_client.cpp`、`mqtt_bridge.cpp`、`lesson6/Makefile`

### 二、HTTP 服务器查询参数支持

**背景**：HTTP 服务器不支持 `?` 查询参数，导致：
- 前端 `img.src = 'camera_capture.jpg?t=xxx'` 返回 404
- `api/camera/view?file=xxx` 返回 404
- `api/logs?level=error` 返回 404

**变更**：
- **`handle_static_file()`**：路径中遇到 `?` 截断，只取前面部分匹配文件
- **`handle_request()`**：API 路由匹配使用去除了查询参数的路径，但传给 handler 的 path 保留完整参数
- 静态文件和 API 路由现在都支持 `?` 查询参数

**涉及文件**：`http_server.c`

### 三、摄像头首帧黑屏修复

**背景**：摄像头刚初始化时自动曝光/AWB 未稳定，首帧常全黑。

**变更**：
- `camera_capture_jpeg()` 内部丢弃前 5 帧，取稳定后的帧保存
- lesson5（rpc_server）和 lesson6（已改用 RPC 调用）均修复

**涉及文件**：`camera_manager.c`

### 四、Web 前端摄像头显示修复

**背景**：点抓拍后图片能拍到（文件正常），但网页上不显示。

**变更**：
- `capturePhoto()` 中 `img.src` 改为绝对路径 `'/' + d.file + '?t=' + Date.now()`
- 图片样式改为 `width:100%;height:auto;margin:0 auto` 居中铺满

**涉及文件**：`www/index.html`

### 五、云端钉钉通知修复

**背景**：每次烟雾报警钉钉收到的图片是上一次报警的旧图（始终慢一拍）。

**根因**：`handle_alert()` 中 `if latest_smoke_image ... send return` 逻辑直接用最近一张图发送，不等当前图片到达。

**变更**：删除快捷判断，每次报警都设置 `pending_alert` 等待 HTTP 图片上传完成再发钉钉。

**涉及文件**：`cloud/mqtt_to_influxdb.py`（阿里云上运行的是 `.bak1` 硬编码版，用户手动同步修改）

### 六、项目文档更新

- **CLAUDE.md**：全面重写为 453 行完整技术文档
- **.trae/documents/项目.md**：全新重写为 1281 行详细项目文档
- **CURRENT.md**：本次变更记录

### 七、上库前清理

- 删除 `脚本.txt`（根目录旧版云端脚本）
- 删除 `cloud/mqtt_to_influxdb.py.bak`、`.bak1`、`代码.txt`
- 修复 `check_all.sh` 测试编译命令指向 `shared_lib` 路径
- 更新 `.gitignore` 添加 `temp.txt`、`temp/`、`*.png` 过滤

### 八、后续优化

1. **check_all.sh 优化**：修复 sysfs 误报、硬编码IP白名单、增加 Python 依赖检查
2. **单元测试扩展**：为 data_cache、msg_queue、crypto_utils、memory_pool 四个模块编写 18 个测试用例，覆盖环形缓冲区、消息队列、SHA-256/XOR/脱敏、内存池分配释放
3. **部署脚本**：`deploy.sh` 支持 `cloud`（云端）、`board <IP>`（板端）、`test`（单元测试）、`build`（编译）四个模式
4. **云端凭据清理**：`cloud/mqtt_to_influxdb.py` 中 INFLUXDB_TOKEN、MQTT_PASS、DINGTALK_WEBHOOK 默认值清空，强制通过环境变量设置
