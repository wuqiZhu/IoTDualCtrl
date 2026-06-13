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
- 包含：`MQTT_HOST=8.140.232.52`、`MQTT_USERNAME=zhuxiangbo`、`MQTT_PASSWORD=<your_password>` 等
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

---

## 2026-06-09 开发记录

### 一、SHA-256 单元测试修复

**背景**：SHA-256 已知答案测试（sha256("abc") = ba7816bf...）在 x86_64 Ubuntu VM（GCC 7.5.0）上失败，输出始终为 `51a5eeba...`。

**根因**：GCC 7.5.0 x86_64 对纯 C 实现的 SHA-256 存在代码生成 bug。5 个独立实现（包括 Brad Conte 的公有域实现）均产生相同错误输出。OpenSSL 的 SHA-256 正常（使用 x86_64 汇编优化）。

**修复**：
- `test_cases.c`：Linux x86_64 平台直接调用 OpenSSL 的 `SHA256()` 验证已知答案
- `check_all.sh` / `deploy.sh`：单元测试编译加 `-lcrypto`
- ARM 交叉编译场景继续使用原有纯 C 实现

**结论**：x86_64 GCC 7.5.0 的代码生成 bug，ARM 交叉编译器（arm-buildroot-linux-gnueabihf-gcc 7.5.0）不受影响，项目生产代码无需修改。

---

## 2026-06-13 开发记录

### 一、清理残留文件

**变更**：
- 删除 `temp.txt`、`temp2.txt`、`temp/` 目录（临时测试文件）
- 删除 `lesson6/test_sha256_*.c` 共 8 个 SHA-256 算法探索试验文件
- 删除 `lesson5/rpc_server/test5.jpg`（测试图片）
- 删除 `__pycache__/`、`.rpc_server.c.swp`（vim 交换文件）
- 删除 jsonrpc-c 和 libev 的 `.o`/`.lo` 中间编译产物（保留源码和 `.a`/`.so`）
- 删除 Qt 客户端（LED_and_TempHumi）的编译产物：`.o`、moc_自动生成文件、二进制

### 二、分离求职自动化脚本

将项目中夹杂的求职脚本迁移到独立仓库 `../find_job`：
- `new_main.py` → `find_job/job_scraper/main.py`
- `new_profile.json` → `find_job/profile.json`
- `new_workflow.yml` → `find_job/.github/workflows/job-scraper.yml`
- 从本仓库删除所有 `new_*` 文件

### 三、修复竞态条件 (`lesson6/mqtt_bridge.cpp`)

**问题**：`fan_state`、`led_state` 被遥测线程和命令工作线程共享，无互斥锁保护。

**修复**：新增 `pthread_mutex_t state_mutex`，所有 6 处读和 7 处写操作均加锁保护：
- **读模式**：`lock → copy → unlock`，RPC 调用在锁外执行，不阻塞 I/O
- **写模式**：`lock → write → unlock`
- 受保护函数：`auto_control_smoke()`、`auto_control_temp()`、`auto_control_light_pir()`、`handle_rpc_control()`

### 四、修复 MQTT 断连检测 (`lesson6/mqtt_bridge.cpp`)

**问题**：MQTT 断开后 `mqtt_connected` 永远不会被置 0，重连逻辑 (`mqtt_reconnect`) 永远不触发。

**修复**：
- `mqtt_publish` 失败时设置 `mqtt_connected = 0`，触发下次循环重连
- `publish_response()`、`publish_alert()` 增加 `mqtt_connected` 检查，断线时放弃发送
- `mqtt_reconnect()` 改为递增等待：5s → 10s → 15s → 20s → 30s（逐步退避）
- -4 错误码（TCP 连接失败）输出中文说明

### 五、文档同步更新

- `CLAUDE.md`：单元测试编译命令修正为完整版（含所有源文件和 `-lpthread -lcrypto`）
- `CLAUDE.md`：已知问题标记 3 项已修复（竞态条件、check_all.sh、测试编译命令）
- `CURRENT.md`：本次变更记录

### 六、摄像头 HTTP 上传改为异步线程 (`lesson6/mqtt_bridge.cpp`)

**问题**：烟雾报警时 `publish_image_http` 在遥测线程中同步执行 HTTP POST 上传，可能阻塞 30 秒（HTTP 超时），导致遥测周期（5 秒）延迟、传感器响应不及时。

**修复**：
- 拍照 + 读文件仍在原线程执行（毫秒级完成）
- HTTP 上传和 MQTT 发布分离到 `image_upload_thread` 独立线程
- 上传线程结束时自动释放资源（`pthread_detach`）
- HTTP 失败时的 Base64 回退也在上传线程中执行，不阻塞主循环

### 七、安全加固（多个文件）

**OTA 命令注入修复** (`lesson6/ota_manager.c`)
- 所有 6 处 `system()` 调用替换为 `safe_execv()`（fork + execvp）
- 消除 shell 注入风险，参数不经过 sh 解释
- 涉及：wget 下载、cp 备份/安装、systemctl 重启、回滚操作

**HTTP 路径遍历修复** (`http_server.c` + `web_api.c`)
- `handle_static_file()` 拒绝含 `..` 的路径
- `api_camera_view()` 增加 `..` 检查 + 文件大小限制 10MB 防 OOM

**Token 可预测修复** (`lesson6/device_auth.c`)
- `generate_random_token()` 改用 `/dev/urandom`
- 应急回退保留 `srand+rand` 但仅 urandom 不可用时

**登录暴力破解防护** (`web_api.c`)
- 连续 3 次登录失败后延迟 1 秒响应
- Session 文件改为 0600 权限

**popen 替换为系统 API** (`web_api.c`)
- `popen("ls -t /tmp/camera_*.jpg")` → `opendir + readdir`
- `popen("hostname")` → `gethostname()`

### 八、更新旧版客户端（4 个文件）

**rpc_client.c（CLI）** — 全面重构
- 提取 `rpc_call_int()` 公共函数，消除 6 份重复的 send/read/parse 代码
- 新增 `rpc_relay_read` / `rpc_relay2_read` / `rpc_camera_capture_jpeg`
- 新增 `-s <ip>` 选项支持指定 RPC 服务器地址
- `sprintf` → `snprintf` 消除缓冲区溢出风险

**Qt 客户端** — rpc_client + mainwindow 优化
- `rpc_client.h/cpp` 新增 `rpc_camera_capture_jpeg`
- `Makefile` 清理无用的 jsonrpc-c/libev 库链接

### 九、Qt 界面重做（3 个文件）

**UI 层面**：
- 标签名 `label`/`label_2` → `value_humi`/`value_temp` 等可读名称
- 传感器面板添加单位标签（%/℃）
- 控制面板显示设备真实状态（🟢已开启/🔴已关闭），从设备回读
- 新增摄像头抓拍按钮 + 退出按钮
- 传感器值用颜色区分（温度红/湿度蓝/烟雾橙）
- 状态栏显示 RPC 连接状态 + 操作结果反馈（2-3 秒提示）

**逻辑层面**：
- 继电器按钮改为先读状态再翻转（从设备真实读取）
- `refreshRelayStates()` 定时 3 秒同步继电器真实状态
- `dht11_thread` 改为发射纯数值，由 UI 层格式化（避免单位重复）
- 析构时等待 DHT11 线程安全退出

### 十、统一 rpc.h 到 shared_lib（4 个文件）

- 新增 `shared_lib/include/rpc.h` 作为 `#define PORT 1234` 的唯一权威定义
- 其他 3 份（rpc_server、rpc_client、lesson6）+ Qt 端改为 `#include` 引用
- `shared.h` 清理 10 个不存在的头文件引用（error.h/log.h/config.h 实际在 lesson6/）
