#!/bin/bash
set -e

echo "=========================================="
echo "项目检查脚本"
echo "=========================================="

# 一、环境检查
echo ""
echo "--- 1. 环境检查 ---"
which arm-buildroot-linux-gnueabihf-gcc && echo "✅ 工具链已安装" || echo "❌ 工具链未安装"

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
test -f requirements.txt && echo "✅ requirements.txt存在" || echo "❌ requirements.txt不存在"

# 四、代码检查
echo ""
echo "--- 4. 代码检查 ---"
grep "/sys" lesson5/rpc_server/rpc_server.c && echo "❌ rpc_server.c仍有sysfs操作" || echo "✅ rpc_server.c已使用HAL"
grep -rn "8\.140\." --include="*.c" --include="*.cpp" --include="*.h" && echo "❌ 发现硬编码IP" || echo "✅ 无硬编码IP"

# 五、单元测试
echo ""
echo "--- 5. 单元测试 ---"
cd lesson6 && gcc -DTEST_MAIN -o test test_cases.c error.c config.c cJSON.c -lm -I. && echo "✅ 测试编译成功" || echo "❌ 测试编译失败"
./test && echo "✅ 测试通过" || echo "❌ 测试失败"
rm -f test
cd ..

echo ""
echo "=========================================="
echo "检查完成"
echo "=========================================="
