#!/usr/bin/env python3
"""
MQTT → InfluxDB 桥接服务 + 图片HTTP服务器 + 钉钉告警
阿里云部署版本 — 从开发板接收遥测、告警、图片，写入InfluxDB并通过钉钉通知

环境变量说明（优先使用环境变量，不设置则使用默认值）：
  INFLUXDB_TOKEN       InfluxDB访问令牌
  MQTT_PASS            MQTT密码
  DINGTALK_WEBHOOK     钉钉机器人Webhook地址
  INFLUXDB_URL         InfluxDB地址（默认 http://localhost:8086）
  INFLUXDB_ORG         组织（默认 influxdb_8KDcrG）
  INFLUXDB_BUCKET      桶名（默认 influxdb_QPKwNw）
  MQTT_BROKER          MQTT地址（默认 localhost）
  MQTT_PORT            MQTT端口（默认 1883）
  MQTT_USER            MQTT用户名（默认 zhuxiangbo）
"""

import json, base64, os, time, threading, requests, logging
import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
from http.server import HTTPServer, BaseHTTPRequestHandler

# ===== 日志配置 =====
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# ===== 配置（优先级：环境变量 > 默认值）=====
# 敏感信息必须通过环境变量设置：INFLUXDB_TOKEN、MQTT_PASS、DINGTALK_WEBHOOK
INFLUXDB_URL = os.environ.get("INFLUXDB_URL", "http://localhost:8086")
INFLUXDB_TOKEN = os.environ.get("INFLUXDB_TOKEN", "")
INFLUXDB_ORG = os.environ.get("INFLUXDB_ORG", "influxdb_8KDcrG")
INFLUXDB_BUCKET = os.environ.get("INFLUXDB_BUCKET", "influxdb_QPKwNw")

MQTT_BROKER = os.environ.get("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_USER = os.environ.get("MQTT_USER", "zhuxiangbo")
MQTT_PASS = os.environ.get("MQTT_PASS", "")

# 图片服务器地址（钉钉消息中图片的URL）
IMAGE_SERVER_URL = os.environ.get("IMAGE_SERVER_URL", "http://localhost:9090")

# 订阅主题：所有需要处理的消息
MQTT_TOPICS = [
    "device/telemetry",   # 遥测数据（事件驱动上报）
    "device/response",    # 控制指令响应
    "device/alert",       # 告警 + 图片上传通知 + 错误报告
    "device/heartbeat",   # 设备心跳
]

DINGTALK_WEBHOOK = os.environ.get("DINGTALK_WEBHOOK", "")

# 敏感信息检查
if not INFLUXDB_TOKEN:
    logger.error("INFLUXDB_TOKEN 环境变量未设置")
    exit(1)
if not MQTT_PASS:
    logger.error("MQTT_PASS 环境变量未设置")
    exit(1)

logger.info(f"InfluxDB: {INFLUXDB_URL} org={INFLUXDB_ORG} bucket={INFLUXDB_BUCKET}")
logger.info(f"MQTT: {MQTT_BROKER}:{MQTT_PORT} user={MQTT_USER}")
logger.info(f"DingTalk webhook: {'已设置' if DINGTALK_WEBHOOK else '未设置'}")

# ===== InfluxDB客户端 =====
influx_client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
write_api = influx_client.write_api(write_options=SYNCHRONOUS)

# 验证InfluxDB连接
try:
    health = influx_client.health()
    if health.status == "pass":
        logger.info(f"InfluxDB connected: {health.message}")
    else:
        logger.warning(f"InfluxDB health: {health.status}")
except Exception as e:
    logger.warning(f"InfluxDB initial connect failed: {e} — will retry at write time")

# ===== 图片服务 =====
IMAGE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images")
os.makedirs(IMAGE_DIR, exist_ok=True)
latest_smoke_image = None        # 最新烟雾照片路径
pending_alert = None             # 待发送的告警（等待图片）
pending_lock = threading.Lock()  # 保护pending_alert的线程安全


class ImageHTTPHandler(BaseHTTPRequestHandler):
    """支持GET下载和POST上传的HTTP服务器（端口9090）"""
    def do_GET(self):
        if self.path.startswith('/images/'):
            filepath = os.path.join(IMAGE_DIR, os.path.basename(self.path))
            if os.path.exists(filepath):
                self.send_response(200)
                self.send_header('Content-Type', 'image/jpeg')
                self.end_headers()
                with open(filepath, 'rb') as f:
                    self.wfile.write(f.read())
                return
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        if self.path == '/upload':
            content_len = int(self.headers.get('Content-Length', 0))
            event_type = self.headers.get('X-Event-Type', 'smoke_alert')
            body = self.rfile.read(content_len)
            fn = f"smoke_{int(time.time())}.jpg"
            fp = os.path.join(IMAGE_DIR, fn)
            with open(fp, 'wb') as f:
                f.write(body)
            global latest_smoke_image, pending_alert
            latest_smoke_image = fp
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "file": fn}).encode())
            logger.info(f"[HTTP] 图片已接收: {fn} ({content_len} bytes), event={event_type}")
            # 如果有待处理的告警，立即发送带图的钉钉
            with pending_lock:
                pa = pending_alert
                if pa:
                    pending_alert = None
            if pa:
                send_alert_with_image(pa["method"], pa["level"], pa["ts"])
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass  # 不打印HTTP访问日志


def start_image_server():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    server = HTTPServer(("0.0.0.0", 9090), ImageHTTPHandler)
    logger.info("图片HTTP服务已启动: http://0.0.0.0:9090 (GET下载 + POST上传)")
    server.serve_forever()


threading.Thread(target=start_image_server, daemon=True).start()


# ===== 钉钉通知 =====
def send_dingtalk(title, content):
    if not DINGTALK_WEBHOOK:
        return
    try:
        payload = {
            "msgtype": "markdown",
            "markdown": {
                "title": title,
                "text": f"## 【通知】{title}\n\n{content}\n\n---\n*IoT 环境监控系统*"
            }
        }
        resp = requests.post(DINGTALK_WEBHOOK, json=payload, timeout=10)
        r = resp.json()
        if resp.status_code == 200 and r.get("errcode") == 0:
            logger.info(f"钉钉发送成功: {title}")
        else:
            logger.warning(f"钉钉发送失败: {resp.status_code} {resp.text}")
    except Exception as e:
        logger.error(f"钉钉异常: {e}")


def send_alert_with_image(alert_type, level_str, ts):
    content = f"级别: {level_str}\n"
    if ts:
        content += f"时间: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(int(ts)))}\n"
    if latest_smoke_image and os.path.exists(latest_smoke_image):
        content += f"\n![现场照片]({IMAGE_SERVER_URL}/images/{os.path.basename(latest_smoke_image)})"
    send_dingtalk(f"🚨 告警: {alert_type}", content)


# ===== MQTT回调 =====
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("MQTT已连接")
        for topic in MQTT_TOPICS:
            client.subscribe(topic)
            logger.info(f"已订阅: {topic}")
    else:
        logger.error(f"MQTT连接失败, rc={rc}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.warning(f"MQTT意外断开 (rc={rc}), 将自动重连")


def handle_telemetry(data):
    """处理遥测数据 → 写入InfluxDB"""
    telem_data = data.get("data")
    if not telem_data:
        return False
    point = Point("sensor_data").tag("device", "imx6ull")
    field_map = {
        "pir": "pir",
        "light": "light",
        "humi": "humidity",
        "temp": "temperature",
        "relay": "relay",
        "relay2": "relay2",
        "smoke_digital": "smoke_digital",
    }
    written = False
    for key, field in field_map.items():
        if key in telem_data:
            try:
                point.field(field, int(telem_data[key]))
                written = True
            except (ValueError, TypeError):
                pass
    if written:
        write_api.write(bucket=INFLUXDB_BUCKET, record=point)
        logger.info(f"遥测写入: pir={telem_data.get('pir')} "
                    f"light={telem_data.get('light')} "
                    f"temp={telem_data.get('temp')} "
                    f"humi={telem_data.get('humi')}")
    return written


def handle_alert(data):
    """处理告警消息 → 钉钉通知"""
    global pending_alert
    level_num = data.get("level", 1)
    level_str = "HIGH" if level_num >= 1 else "LOW"
    ts = data.get("timestamp", time.time())
    logger.info(f"收到告警 [{level_str}] ，等待图片...")

    # 总是等待当前这次报警对应的图片，避免误用上一条报警的旧图
    # 图片会在几秒内通过HTTP上传到达，届时触发钉钉发送
    with pending_lock:
        pending_alert = {"method": data.get("method", "alert"),
                         "level": level_str, "ts": ts, "time": time.time()}


def handle_image_uploaded(data):
    """处理HTTP图片上传成功通知（MQTT小通知）"""
    event = data.get("event", "unknown")
    ts = data.get("timestamp", "")
    logger.info(f"[NOTIFY] 图片已通过HTTP上传: event={event}, timestamp={ts}")
    # 如果已有图片且有待发送告警，立即触发
    global pending_alert
    if latest_smoke_image and os.path.exists(latest_smoke_image):
        with pending_lock:
            pa = pending_alert
            if pa:
                pending_alert = None
        if pa:
            send_alert_with_image(pa["method"], pa["level"], pa["ts"])


def handle_image_upload(data):
    """处理MQTT base64图片上传（HTTP上传失败时的回退方案）"""
    img_data = data.get("image_data", "")
    if not img_data:
        return
    try:
        img_bytes = base64.b64decode(img_data)
        fn = f"smoke_{int(time.time())}.jpg"
        fp = os.path.join(IMAGE_DIR, fn)
        with open(fp, "wb") as f:
            f.write(img_bytes)
        global latest_smoke_image, pending_alert
        latest_smoke_image = fp
        logger.info(f"[MQTT] 图片已保存: {fn} ({len(img_bytes)} bytes)")
        with pending_lock:
            pa = pending_alert
            if pa:
                pending_alert = None
        if pa:
            send_alert_with_image(pa["method"], pa["level"], pa["ts"])
    except Exception as e:
        logger.error(f"保存MQTT图片失败: {e}")


def handle_error_report(data):
    """处理错误报告 → 写入InfluxDB + 钉钉通知"""
    level = data.get("level", 0)
    module = data.get("module", "unknown")
    func = data.get("function", "")
    code = data.get("error_code", 0)
    message = data.get("message", "")
    device_id = data.get("device_id", "")
    ts = data.get("timestamp", time.time())

    # 写入InfluxDB
    point = Point("error_report").tag("device", "imx6ull").tag("module", module)
    point.field("level", level)
    point.field("error_code", code)
    if device_id:
        point.tag("device_id", device_id)
    try:
        write_api.write(bucket=INFLUXDB_BUCKET, record=point)
    except Exception as e:
        logger.warning(f"错误报告写入InfluxDB失败: {e}")

    # 高/严重错误才发钉钉
    level_names = ["LOW", "MEDIUM", "HIGH", "CRITICAL"]
    level_name = level_names[level] if 0 <= level < len(level_names) else "UNKNOWN"
    logger.info(f"[ERROR_REPORT] [{level_name}] {module}.{func}: {message} (code={code})")

    if level >= 2:  # HIGH 或 CRITICAL
        content = (f"模块: {module}.{func}\n"
                   f"级别: {level_name}\n"
                   f"错误码: {code}\n"
                   f"详情: {message}\n")
        if device_id:
            content += f"设备: {device_id}\n"
        if ts:
            content += f"时间: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(int(ts)))}\n"
        send_dingtalk(f"⚠️ 设备错误: {module}", content)


def handle_heartbeat(data):
    """处理心跳数据 → 写入InfluxDB"""
    device_id = data.get("device_id", "imx6ull")

    # 写入设备心跳指标
    point = Point("device_heartbeat").tag("device", device_id)
    cpu = data.get("cpu_usage")
    if cpu is not None:
        point.field("cpu_usage", float(cpu))

    mem = data.get("memory", {})
    if mem:
        mem_total = mem.get("total_kb")
        mem_avail = mem.get("available_kb")
        mem_pct = mem.get("usage_percent")
        if mem_total is not None:
            point.field("mem_total_kb", int(mem_total))
        if mem_avail is not None:
            point.field("mem_available_kb", int(mem_avail))
        if mem_pct is not None:
            point.field("mem_usage_percent", float(mem_pct))

    load_avg = data.get("load_avg", {})
    if load_avg:
        for key in ("1min", "5min", "15min"):
            val = load_avg.get(key)
            if val is not None:
                point.field(f"load_{key}", float(val))

    uptime = data.get("uptime", {})
    if uptime:
        seconds = uptime.get("seconds")
        if seconds is not None:
            point.field("uptime_seconds", float(seconds))

    status = data.get("status", "")
    if status:
        point.field("status", status)

    try:
        write_api.write(bucket=INFLUXDB_BUCKET, record=point)
        logger.info(f"心跳写入: device={device_id} cpu={cpu}%")
    except Exception as e:
        logger.warning(f"心跳写入失败: {e}")


def handle_response(data):
    """处理控制指令响应 → 写入InfluxDB"""
    method = data.get("method", "unknown")
    success = data.get("success", 0)
    if success != 1:
        return  # 不记录失败响应

    point = Point("sensor_data").tag("device", "imx6ull")
    resp_data = data.get("data")
    handled = True

    if method == "dht11_read" and resp_data:
        humi = resp_data.get("humi") if isinstance(resp_data, dict) else None
        temp = resp_data.get("temp") if isinstance(resp_data, dict) else None
        if humi is not None:
            point.field("humidity", int(humi))
        if temp is not None:
            point.field("temperature", int(temp))
    elif method in ("pir_read",) and resp_data:
        val = resp_data.get("pir") if isinstance(resp_data, dict) else None
        if val is not None:
            point.field("pir", int(val))
    elif method in ("light_read",) and resp_data:
        light_val = resp_data.get("light") if isinstance(resp_data, dict) else None
        if isinstance(light_val, list):
            light_val = light_val[0] if light_val else 0
        if light_val is not None:
            point.field("light", int(light_val))
    elif method in ("smoke_digital_read",) and resp_data:
        val = resp_data.get("smoke_digital") if isinstance(resp_data, dict) else None
        if val is not None:
            point.field("smoke_digital", int(val))
    elif method in ("relay_control", "relay2_control"):
        point.field(f"{method}_success", 1)
    elif method in ("system_status", "sensor_status", "firmware_version"):
        # 这些响应只是状态查询，不需要写入InfluxDB
        return
    else:
        handled = False

    if handled:
        try:
            write_api.write(bucket=INFLUXDB_BUCKET, record=point)
            logger.info(f"响应写入: {method}")
        except Exception as e:
            logger.warning(f"响应写入失败 ({method}): {e}")
    else:
        logger.debug(f"未处理的响应方法: {method}")


def on_message(client, userdata, msg):
    global latest_smoke_image, pending_alert
    try:
        data = json.loads(msg.payload.decode())
        method = data.get("method", "unknown")

        # 日志：简要打印每个消息
        topic_simple = msg.topic.split('/')[-1] if msg.topic else '?'
        payload_preview = msg.payload[:80].decode(errors='replace') + (
            '...' if len(msg.payload) > 80 else '')
        logger.debug(f"[MSG] topic={topic_simple} method={method} "
                     f"size={len(msg.payload)} preview={payload_preview}")

        # ---- 按主题分流 ----
        if msg.topic == "device/telemetry":
            handle_telemetry(data)
            return

        if msg.topic == "device/heartbeat":
            handle_heartbeat(data)
            return

        if msg.topic == "device/response":
            handle_response(data)
            return

        if msg.topic == "device/alert":
            # alert主题包含多种消息类型
            if method == "image_upload":
                # MQTT base64图片上传（HTTP上传回退方案）
                handle_image_upload(data)
            elif method == "image_uploaded":
                # HTTP图片上传成功通知
                handle_image_uploaded(data)
            elif method == "error_report":
                # 错误报告
                handle_error_report(data)
            elif method in ("smoke_alert", "alert"):
                # 告警消息
                handle_alert(data)
            else:
                logger.debug(f"未处理的alert方法: {method}")
            return

        logger.debug(f"未处理的消息: topic={msg.topic} method={method}")

    except json.JSONDecodeError as e:
        logger.error(f"JSON解析失败: {e}")
    except Exception as e:
        logger.exception(f"处理消息异常: {e}")


# ===== 超时检查线程 =====
def check_pending():
    """检查待发送告警是否超时（等待图片超30秒则不带图发送）"""
    global pending_alert
    while True:
        time.sleep(3)
        with pending_lock:
            pa = pending_alert
            if not pa:
                continue
            if time.time() - pa["time"] <= 30:
                continue
            # 超时了——先看是否有HTTP上传的图片
            img_files = sorted(
                [f for f in os.listdir(IMAGE_DIR) if f.startswith("smoke_")],
                reverse=True
            )
            found = False
            if img_files:
                latest = os.path.join(IMAGE_DIR, img_files[0])
                img_time = os.path.getmtime(latest)
                if time.time() - img_time < 60:
                    global latest_smoke_image
                    latest_smoke_image = latest
                    logger.info(f"超时但找到本地图片: {img_files[0]}")
                    send_alert_with_image(pa["method"], pa["level"], pa["ts"])
                    found = True
            if not found:
                logger.info("图片未收到（超时30秒），发送不带图的告警")
                send_alert_with_image(pa["method"], pa["level"], pa["ts"])
            pending_alert = None


threading.Thread(target=check_pending, daemon=True).start()


# ===== 启动MQTT =====
client = mqtt.Client()
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.reconnect_delay_set(min_delay=1, max_delay=60)

try:
    logger.info("启动MQTT...")
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()
except KeyboardInterrupt:
    logger.info("收到中断信号，退出")
    client.disconnect()
except Exception as e:
    logger.exception(f"启动失败: {e}")
    exit(1)
