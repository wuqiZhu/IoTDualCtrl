#!/bin/bash
set -e

echo "=========================================="
echo "项目检查脚本"
echo "=========================================="

# 一、环境检查
echo ""
echo "--- 1. 环境检查 ---"
which arm-buildroot-linux-gnueabihf-gcc && echo "✅ 工具链已安装" || echo "❌ 工具链未安装"
which python3 && echo "✅ python3 已安装" || echo "❌ python3 未安装"

# 二、编译检查
echo ""
echo "--- 2. 编译检查 ---"
cd lesson5/rpc_server && make clean && make && echo "✅ rpc_server编译成功" || echo "❌ rpc_server编译失败"
cd ../../lesson5/rpc_client && make clean && make && echo "✅ rpc_client编译成功" || echo "❌ rpc_client编译失败"
cd ../../lesson6 && make clean && make && echo "✅ mqtt_bridge编译成功" || echo "❌ mqtt_bridge编译失败"
cd ..

# 三、配置检查
echo ""
echo "--- 3. 配置检查 ---"
python3 -c "import json; json.load(open('config.json'))" && echo "✅ config.json格式正确" || echo "❌ config.json格式错误"
test -f .env.example && echo "✅ .env.example存在" || echo "❌ .env.example不存在"
test -f cloud/requirements.txt && echo "✅ cloud/requirements.txt存在" || echo "❌ cloud/requirements.txt不存在"

# 四、代码检查
echo ""
echo "--- 4. 代码检查 ---"
# 检查是否有直接 sysfs 操作（排除 /api/ 路径）
grep '/sys' lesson5/rpc_server/rpc_server.c | grep -v '/api/' && echo "❌ rpc_server.c仍有sysfs操作" || echo "✅ rpc_server.c已使用HAL"

# 检查硬编码IP（排除已知合法的阿里云服务器地址）
grep -rn "8\.140\." --include="*.c" --include="*.cpp" --include="*.h" --include="*.py" | grep -v "8\.140\.232\.52" && echo "❌ 发现未知硬编码IP" || echo "✅ 无未知硬编码IP"

# 检查是否有硬编码凭据（仅扫描关键文件，避免误报）
echo "检查敏感信息（仅提示，请手动确认）..."
grep -rn "INFLUXDB_TOKEN\s*=\s*\"[a-zA-Z0-9_\-]\{10,\}\"" cloud/ --include="*.py" && echo "⚠️ 发现可能的硬编码INFLUXDB_TOKEN" || echo "✅ 无硬编码INFLUXDB_TOKEN"
grep -rn "MQTT_PASS\s*=\s*\"[a-zA-Z0-9_\-@]\{4,\}\"" cloud/ --include="*.py" && echo "⚠️ 发现可能的硬编码MQTT_PASS" || echo "✅ 无硬编码MQTT_PASS"

# 五、Python依赖检查
echo ""
echo "--- 5. Python依赖检查 ---"
if [ -f cloud/requirements.txt ]; then
  pip3 install -q -r cloud/requirements.txt 2>/dev/null && echo "✅ Python依赖已安装" || echo "❌ Python依赖安装失败"
fi

# 六、单元测试
echo ""
echo "--- 6. 单元测试 ---"
cd lesson6 && gcc -DTEST_MAIN -o test test_cases.c error.c config.c data_cache.c msg_queue.c crypto_utils.c memory_pool.c -I../shared_lib/include ../shared_lib/src/cJSON.c -lm -lpthread -I. -lcrypto 2>&1 && echo "✅ 测试编译成功" || echo "❌ 测试编译失败"
if [ -f ./test ]; then
  ./test && echo "✅ 测试通过" || echo "❌ 测试失败"
  rm -f test
fi
cd ..

echo ""
echo "=========================================="
echo "检查完成"
echo "=========================================="
