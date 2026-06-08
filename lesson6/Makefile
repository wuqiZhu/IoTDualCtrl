# ==========================================================================
# MQTT Bridge Makefile
# ==========================================================================

TARGET = mqtt_bridge
CC = arm-buildroot-linux-gnueabihf-gcc
CXX = arm-buildroot-linux-gnueabihf-g++

# ==========================================================================
# 编译选项
# ==========================================================================

# 共享库路径
SHARED_DIR = ../shared_lib
SHARED_INC = -I$(SHARED_DIR)/include

# 头文件路径
INCLUDE_PATH = -I include/test/ \
               -I include/common/ \
               -I include/mqttclient \
               -I include/ \
               -I include/common/log \
               -I include/mqtt \
               -I ./include/platform/linux \
               -I ./include/network \
               $(SHARED_INC)

# 库文件路径
LIB_PATH = -L .

# ==========================================================================
# 源文件
# ==========================================================================

# C++源文件（cJSON.cpp → 移到 shared_lib）
CPP_SRCS = mqtt_bridge.cpp rpc_client.cpp

# C源文件（watchdog.c → 移到 shared_lib）
C_SRCS = config.c log.c error.c data_cache.c system_monitor.c device_auth.c \
         msg_queue.c sensor_manager.c ota_manager.c \
         security_audit.c crypto_utils.c memory_pool.c perf_monitor.c \
         plugin_manager.c device_discovery.c

# 共享库源文件（从 shared_lib 编译，用改名避免冲突）
SHARED_C_SRCS = $(SHARED_DIR)/src/cJSON.c $(SHARED_DIR)/src/watchdog.c
SHARED_C_OBJS = cjson_shared.o watchdog_shared.o

# 目标文件
CPP_OBJS = $(CPP_SRCS:.cpp=.o)
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(CPP_OBJS) $(C_OBJS) $(SHARED_C_OBJS)

# ==========================================================================
# 编译标志
# ==========================================================================

# 通用标志
COMMON_FLAGS = -Wall -Wextra -Werror

# Debug标志
DEBUG_FLAGS = -O0 -g -DDEBUG

# Release标志
RELEASE_FLAGS = -O2 -DNDEBUG

# 默认使用Release模式
CXXFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS) $(INCLUDE_PATH)
CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS) $(INCLUDE_PATH)
LDFLAGS = $(LIB_PATH) -lmqttclient -lpthread -ldl

# ==========================================================================
# 构建目标
# ==========================================================================

# 默认目标
all: $(TARGET)

# Debug目标
debug: CXXFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS) $(INCLUDE_PATH)
debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS) $(INCLUDE_PATH)
debug: $(TARGET)

# Release目标
release: CXXFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS) $(INCLUDE_PATH)
release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS) $(INCLUDE_PATH)
release: $(TARGET)

# ==========================================================================
# 链接规则
# ==========================================================================

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# ==========================================================================
# 编译规则
# ==========================================================================

# C++编译规则
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 共享库源文件编译规则（从shared_lib编译）
cjson_shared.o: $(SHARED_DIR)/src/cJSON.c
	$(CC) $(CFLAGS) -c $< -o $@

watchdog_shared.o: $(SHARED_DIR)/src/watchdog.c
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================================================
# 清理规则
# ==========================================================================

clean:
	rm -f $(OBJS) $(TARGET)

# ==========================================================================
# 安装规则（可选）
# ==========================================================================

install: $(TARGET)
	@echo "Installing $(TARGET)..."
	@echo "Please copy $(TARGET) to your target device manually."

# ==========================================================================
# 静态分析工具
# ==========================================================================

# cppcheck静态分析
cppcheck:
	@echo "Running cppcheck..."
	cppcheck --enable=all --std=c11 --std=c++11 \
		--suppress=missingIncludeSystem \
		--suppress=unusedFunction \
		--inline-suppr \
		-I include/ -I$(SHARED_DIR)/include \
		$(C_SRCS) $(CPP_SRCS) $(SHARED_C_SRCS) 2>&1 | tee cppcheck_report.txt
	@echo "cppcheck report saved to cppcheck_report.txt"

# clang-tidy静态分析
clang-tidy:
	@echo "Running clang-tidy..."
	@for src in $(C_SRCS); do \
		echo "Analyzing $$src..."; \
		clang-tidy $$src -- -std=c11 $(INCLUDE_PATH) 2>&1; \
	done | tee clang-tidy_report.txt
	@for src in $(CPP_SRCS); do \
		echo "Analyzing $$src..."; \
		clang-tidy $$src -- -std=c++11 $(INCLUDE_PATH) 2>&1; \
	done | tee -a clang-tidy_report.txt
	@echo "clang-tidy report saved to clang-tidy_report.txt"

# 代码覆盖率分析（需要先用debug模式编译）
coverage: debug
	@echo "Running code coverage analysis..."
	@echo "Note: Run tests first, then use 'gcov' and 'lcov' to generate report"

# 内存检查（需要valgrind）
valgrind: debug
	@echo "Running valgrind memory check..."
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		./$(TARGET) 2>&1 | tee valgrind_report.txt
	@echo "valgrind report saved to valgrind_report.txt"

# 代码行数统计
loc:
	@echo "Lines of Code Statistics:"
	@echo "========================"
	@echo "C source files:"
	@wc -l $(C_SRCS) | tail -1
	@echo "C++ source files:"
	@wc -l $(CPP_SRCS) | tail -1
	@echo "Header files:"
	@find . -name "*.h" -exec wc -l {} + | tail -1
	@echo "========================"
	@echo "Total:"
	@find . -name "*.c" -o -name "*.cpp" -o -name "*.h" | xargs wc -l | tail -1

# TODO/FIXME统计
todo:
	@echo "TODO/FIXME items:"
	@grep -rn "TODO\|FIXME\|HACK\|XXX" $(C_SRCS) $(CPP_SRCS) || echo "No TODO/FIXME found"

# ==========================================================================
# 帮助信息
# ==========================================================================

help:
	@echo "Available targets:"
	@echo "  all       - Build with Release mode (default)"
	@echo "  debug     - Build with Debug mode"
	@echo "  release   - Build with Release mode"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Show installation instructions"
	@echo "  cppcheck  - Run cppcheck static analysis"
	@echo "  clang-tidy - Run clang-tidy static analysis"
	@echo "  coverage  - Run code coverage analysis"
	@echo "  valgrind  - Run valgrind memory check"
	@echo "  loc       - Count lines of code"
	@echo "  todo      - List TODO/FIXME items"
	@echo "  help      - Show this help message"

# ==========================================================================
# 依赖关系（自动依赖生成）
# ==========================================================================

# 如果需要自动依赖生成，可以取消注释以下行
# -include $(OBJS:.o=.d)

.PHONY: all debug release clean install help cppcheck clang-tidy coverage valgrind loc todo
