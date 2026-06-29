#!/bin/bash
# ==========================================================================
# IoT 环境监控系统 - 一键部署脚本
# ==========================================================================
# 用法:
#   ./deploy.sh cloud         部署云端脚本到阿里云
#   ./deploy.sh board <IP>    部署板端程序到开发板
#   ./deploy.sh all <IP>      同时部署云端和板端
#   ./deploy.sh test          运行本地单元测试
#
# 环境变量:
#   CLOUD_HOST    阿里云服务器地址 (默认: 8.140.232.52)
#   CLOUD_USER    SSH用户名 (默认: root)
#   CLOUD_PATH    云端部署路径 (默认: /opt/iot_mqtt_to_influx)
# ==========================================================================

set -e

# 配置
CLOUD_HOST="${CLOUD_HOST:-8.140.232.52}"
CLOUD_USER="${CLOUD_USER:-root}"
CLOUD_PATH="${CLOUD_PATH:-/opt/iot_mqtt_to_influx}"
SHARED_DIR="shared_lib"
LESSON5_DIR="lesson5/rpc_server"
LESSON6_DIR="lesson6"

# 颜色
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查依赖
check_deps() {
  info "检查依赖..."
  command -v ssh >/dev/null 2>&1 || { error "ssh 未安装"; exit 1; }
  command -v scp >/dev/null 2>&1 || { error "scp 未安装"; exit 1; }
  info "依赖检查通过"
}

# ==========================================================================
# 编译
# ==========================================================================
build_all() {
  info "编译共享库..."
  cd "$SHARED_DIR" && make clean && make && cd ..

  info "编译 rpc_server..."
  cd "$LESSON5_DIR" && make clean && make && cd ../..

  info "编译 mqtt_bridge..."
  cd "$LESSON6_DIR" && make clean && make && cd ..

  info "编译完成"
}

# ==========================================================================
# 部署到云端
# ==========================================================================
deploy_cloud() {
  info "部署云端脚本到 $CLOUD_HOST ..."

  # 检查连通性
  ping -c 1 -W 3 "$CLOUD_HOST" >/dev/null 2>&1 || {
    error "无法连接到 $CLOUD_HOST"; exit 1; }

  # 创建目录
  ssh "$CLOUD_USER@$CLOUD_HOST" "mkdir -p $CLOUD_PATH/images"

  # 上传脚本
  scp cloud/mqtt_to_influxdb.py "$CLOUD_USER@$CLOUD_HOST:$CLOUD_PATH/"
  scp cloud/requirements.txt "$CLOUD_USER@$CLOUD_HOST:$CLOUD_PATH/"

  # 安装依赖并启动
  ssh "$CLOUD_USER@$CLOUD_HOST" "cd $CLOUD_PATH && \
    pip3 install -q -r requirements.txt 2>/dev/null; \
    pkill -f mqtt_to_influxdb 2>/dev/null; \
    nohup python3 -u mqtt_to_influxdb.py > mqtt_to_influxdb.log 2>&1 &"

  info "云端部署完成"
  info "查看日志: ssh $CLOUD_USER@$CLOUD_HOST 'tail -f $CLOUD_PATH/mqtt_to_influxdb.log'"
  info "检查端口: ssh $CLOUD_USER@$CLOUD_HOST 'netstat -tlnp | grep 9090'"
}

# ==========================================================================
# 部署到开发板
# ==========================================================================
deploy_board() {
  BOARD_IP=$1
  [ -z "$BOARD_IP" ] && { error "用法: $0 board <开发板IP>"; exit 1; }

  info "部署到开发板 $BOARD_IP ..."

  ping -c 1 -W 3 "$BOARD_IP" >/dev/null 2>&1 || {
    error "无法连接到 $BOARD_IP"; exit 1; }

  # 先编译
  build_all

  # 上传二进制文件
  info "上传 rpc_server..."
  scp "$LESSON5_DIR/rpc_server" "root@$BOARD_IP:/usr/local/bin/"

  info "上传 mqtt_bridge..."
  scp "$LESSON6_DIR/mqtt_bridge" "root@$BOARD_IP:/usr/local/bin/"

  info "上传 Web 前端..."
  scp "$LESSON5_DIR/www/index.html" "root@$BOARD_IP:/usr/local/bin/www/"

  # 上传配置文件
  scp config.json "root@$BOARD_IP:/root/"

  # 重启服务
  info "重启服务..."
  ssh "root@$BOARD_IP" "systemctl restart rpc_server mqtt_bridge"

  info "板端部署完成"
  info "Web 界面: http://$BOARD_IP:8080"
  info "查看日志: ssh root@$BOARD_IP 'journalctl -u rpc_server -f'"
}

# ==========================================================================
# 运行单元测试
# ==========================================================================
run_tests() {
  info "运行单元测试..."
  cd "$LESSON6_DIR"

  gcc -DTEST_MAIN -o test \
    test_cases.c error.c config.c \
    data_cache.c msg_queue.c crypto_utils.c memory_pool.c \
    -I../shared_lib/include ../shared_lib/src/cJSON.c \
    -lm -lpthread -I. -lcrypto

  ./test
  local result=$?
  rm -f test
  cd ..

  if [ $result -eq 0 ]; then
    info "测试全部通过"
  else
    error "测试有失败项"
  fi
  return $result
}

# ==========================================================================
# 主流程
# ==========================================================================
case "${1:-help}" in
  cloud)
    check_deps
    deploy_cloud
    ;;
  board)
    check_deps
    deploy_board "$2"
    ;;
  all)
    check_deps
    deploy_cloud
    deploy_board "$2"
    ;;
  test)
    run_tests
    ;;
  build)
    build_all
    ;;
  *)
    echo "用法: $0 {cloud|board <IP>|all <IP>|test|build}"
    echo ""
    echo "示例:"
    echo "  $0 test              运行本地单元测试"
    echo "  $0 build             编译所有目标"
    echo "  $0 cloud             部署云端脚本到阿里云"
    echo "  $0 board 192.168.1.100  部署到开发板"
    echo "  $0 all 192.168.1.100 同时部署云端和板端"
    exit 1
    ;;
esac
