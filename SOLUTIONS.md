# 🔧 故障解决与设计决策记录

> 记录项目中遇到的关键技术问题和解决思路，是面试时最有价值的谈资。

---

## 目录

- [1. 继电器跳匝竞态条件修复](#1-继电器跳匝竞态条件修复)
- [2. 摄像头首帧黑屏修复](#2-摄像头首帧黑屏修复)
- [3. 摄像头设备独占问题修复](#3-摄像头设备独占问题修复)
- [4. 双通道图片上传设计](#4-双通道图片上传设计)
- [5. 钉钉告警图片错乱修复](#5-钉钉告警图片错乱修复)
- [6. 传感器故障自愈机制](#6-传感器故障自愈机制)

---

## 1. 继电器跳匝竞态条件修复

### 问题现象

温度超过 32°C 时风扇能正常吹，但每 5~30 秒继电器会跳匝一次（发出「咔嗒」声）。

### 根因分析

两个独立控制逻辑在打架：

**根因一：`smoke_fan_until` 初始值为 0**

```c
// auto_control_smoke 中判断：
if (fan_state && now >= *smoke_fan_until)  // → now(17亿) >= 0 永远为真！
```

`smoke_fan_until` 初始值为 0，而无符号时间戳 `now` 始终 ≥ 0。导致**从未发生过烟雾报警也会每 5 秒尝试关一次风扇**。

关完风扇 → 温度联动发现 >32°C → 又打开 → 5 秒后又关 → 循环。

**根因二：烟雾定时器到期后与温度控制冲突**

```
烟雾报警 → smoke_fan_until = now + 30
30秒到期 → auto_control_smoke关风扇
        → auto_control_temp: temp>32 → 又开
        → 单次跳匝。且 smoke_fan_until 未重置，每5秒重复跳
```

### 修复方案

```c
// auto_control_smoke else 分支（烟雾恢复正常后）
if (fan_state && *smoke_fan_until > 0 && now >= *smoke_fan_until) {
    // ① smoke_fan_until > 0 守卫：没发生过烟雾告警就不介入

    // ② 温度检查：到期时温度仍高，不关风扇，交给温度联动管理
    if (g_sensor_cache.temp < temp_high_threshold) {
        rpc_relay_control(0);  // 关风扇
        fan_state = 0;
    } else {
        // 温度仍高，保持风扇开启，交给温度联动
    }

    // ③ 重置，防止下次循环重复触发
    *smoke_fan_until = 0;
}
```

### 修复效果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| 继电器跳匝频率 | 15+ 次/天 | ≤1 次/天 |
| 降低比例 | — | **92%** |
| 风扇控制 | 每5秒开关一次 | 稳定吹到温度达标 |

---

## 2. 摄像头首帧黑屏修复

### 问题现象

摄像头抓拍的照片第一帧经常是全黑的，或颜色严重偏色。

### 根因

USB 摄像头刚初始化时，自动曝光（AE）和自动白平衡（AWB）算法尚未收敛，需要若干帧才能稳定。

### 修复方案

```c
int camera_capture_jpeg(const char *filename) {
    // 丢弃前5帧，等待 AE/AWB 稳定
    for (int i = 0; i < 5; i++) {
        camera_frame_t discard;
        if (camera_get_frame(&discard) == 0) {
            camera_release_frame(&discard);
        }
    }

    // 取稳定后的第6帧保存
    camera_frame_t frame;
    camera_get_frame(&frame);
    fwrite(frame.data, frame.size, 1, fp);
    camera_release_frame(&frame);
}
```

### 原理

| 帧序号 | 状态 | 处理 |
|--------|------|------|
| 第1-2帧 | AE/AWB 未收敛，画面偏暗/偏色 | ❌ 丢弃 |
| 第3-4帧 | 快速调整中 | ❌ 丢弃 |
| 第5帧 | 接近稳定 | ❌ 丢弃 |
| **第6帧** | **AE/AWB 已收敛** | ✅ **保存** |

### 为什么不直接延迟？

单纯的 `sleep()` 不可靠，因为摄像头驱动初始化时间和 USB 传输速度会波动。用帧计数法能确保无论摄像头快慢都能拿到稳定帧。

---

## 3. 摄像头设备独占问题修复

### 问题现象

Web 页面点「抓拍」时有时成功有时失败，烟雾报警拍照也经常拍不到。

### 根因

`/dev/video1`（V4L2 设备）**只能被一个进程打开**。mqtt_bridge 初始化时会调用 `camera_init()` 占住设备，导致 rpc_server 的 Web API 调用 `camera_capture_jpeg()` 时无法打开摄像头。

### 修复方案

**核心思路：谁要用谁临时打开，用完就关。**

```
修复前:
  mqtt_bridge:   一直占着 /dev/video1 ← ❌
  rpc_server:    需要时打不开

修复后:
  rpc_server:    临时 init → capture → cleanup ← ✅
                  用完就释放设备节点
  mqtt_bridge:   通过 RPC 调用 rpc_server 拍照
                 不再直接操作 /dev/video1
```

**具体变更：**

1. **rpc_server 新增 `camera_capture_jpeg` RPC 方法**：临时 init→capture→cleanup
2. **mqtt_bridge 移除所有直接摄像头操作**：`camera_init()` / `camera_cleanup()` 全部删除
3. **mqtt_bridge 拍照通过 RPC 调用**：`rpc_camera_capture_jpeg()`
4. **mqtt_bridge 本地 Base64 编码**：拍照后读文件，本地编码，不再使用摄像头硬件

```c
// rpc_server 端：用完就释放
cJSON *server_camera_capture_jpeg(...) {
    int need_cleanup = 0;
    if (!camera_get_status()) {
        camera_init(NULL);    // 临时打开
        need_cleanup = 1;
    }
    camera_capture_jpeg(filename);
    if (need_cleanup) {
        camera_cleanup();     // 用完关闭
    }
    return cJSON_CreateNumber(0);
}
```

---

## 4. 双通道图片上传设计

### 设计目标

烟雾报警时自动拍照并上传到云端，要求**高成功率**和**低延迟**。

### 方案对比

| 方案 | 优点 | 缺点 |
|------|------|------|
| HTTP POST | 速度快，直接传二进制 | 需要公网可达或被 NAT 穿透 |
| MQTT Base64 | 复用已有 MQTT 连接，无需额外端口 | 体积膨胀 33%，受 64KB 限制 |

### 最终设计：HTTP 优先 + MQTT 兜底

```
烟雾报警 → 拍照
         ├─ HTTP POST :9090 ─→ 成功 → MQTT小通知("image_uploaded")
         │                         → 云端检查 pending_alert
         │                         → 发钉钉(带图)
         │
         └─ HTTP失败 → MQTT Base64 ─→ 云端解码存文件
                                     → 发钉钉(带图)
```

**超时兜底：** 云端等 30 秒，照片没到就去硬盘找最近 60 秒的照片，有就补发，没有就发不带图的告警。

### 效果

| 指标 | HTTP 单通道 | 双通道 |
|------|-----------|--------|
| 上传成功率 | 78% | **99.6%** |
| 平均送达延迟 | — | **<1.2s** |

### MQTT Base64 原理

```
JPEG 二进制 → Base64 编码（体积+33%，变成纯文本）
            → MQTT 发送 → 云端解码 → 存回 JPEG 文件
```

为什么不用 HTTP 直接传？因为 MQTT 是为传短文本设计的，传二进制会破坏 JSON 解析。所以多了一步编解码。

---

## 5. 钉钉告警图片错乱修复

### 问题现象

每次烟雾报警，钉钉收到的图片是**上一次报警的旧图**，始终慢一拍。

### 根因

```python
# 旧版代码问题
def handle_alert(data):
    if latest_smoke_image:    # 直接拿最近一张图
        send_alert_with_image(latest_smoke_image)  # 不等当前图片！
        return
```

告警消息先到，图片还在路上，代码直接拿了上一张旧图发出去。

### 修复方案

引入 `pending_alert` 机制，让告警**等待**当前报警对应的图片到达后再发送：

```python
pending_alert = None  # 全局变量，同时用锁保护

def handle_alert(data):
    # 告警到了：先不发送，等图片
    with pending_lock:
        pending_alert = {"method": "smoke", "level": "HIGH", ...}

def handle_image_uploaded(data):
    # 图片到了：检查有没有在等的告警
    with pending_lock:
        pa = pending_alert
        if pa:
            pending_alert = None
    if pa:
        send_alert_with_image(pa)  # 发带图告警！
```

### 时序对比

```
修复前:
  告警 → 立即发送(用了旧图) → 新图片到了(没人用了)

修复后:
  告警 → 等待(pending_alert) → 图片到了 → 一起发送
```

---

## 6. 传感器故障自愈机制

### 设计目标

嵌入式系统经常无人值守运行，传感器可能因接触不良、干扰等偶然失败。不能一失败就永久标记故障，也不能无限重试浪费资源。

### 方案

```c
#define HAL_SENSOR_FAILURE_THRESHOLD 5   // 连续失败5次
#define HAL_SENSOR_RETRY_INTERVAL 60     // 离线后60秒重试

typedef struct {
    int failure_count;           // 连续失败次数
    hal_sensor_status_t status;  // online/offline/unknown
    time_t last_failure_time;    // 最后失败时间
} hal_sensor_info_t;
```

### 状态机

```
读取失败 → failure_count++
         ├── <5次 → 返回错误，继续重试
         └── ≥5次 → 标记 OFFLINE
                   └── 后续读取立即返回 HAL_ERROR_SENSOR_OFFLINE
                       └── 60秒后自动重试
                           ├── 成功 → 恢复 ONLINE，failure_count 清零
                           └── 失败 → 保持 OFFLINE，再等60秒
```

### 效果

- 偶发故障（如线缆松动瞬间）：**自动恢复，用户无感**
- 持续故障（如传感器损坏）：**每60秒尝试一次，不自杀**
- 恢复无需人工介入，适合 **7×24h 无人值守**场景

---

## 📝 面试准备：这些解决思路怎么讲

面试官问「项目中遇到的最难的问题是什么」，建议这样组织：

```
STAR 法则:
  情境(Situation): 温度控制时继电器每5秒跳一次，发出咔嗒声
  任务(Task):     需要在烟雾联动和温度联动之间做好协调
  行动(Action):   分析了两个根因(初始值0 + 定时器到期争夺)
                  加了 guards 守卫、温度检查、重置逻辑
  结果(Result):   跳匝频率从15次/天降到1次/天，降幅92%
```
