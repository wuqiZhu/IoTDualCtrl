# ==========================================================================
# MQTT Bridge Makefile
# ==========================================================================

TARGET = mqtt_bridge
CC = arm-buildroot-linux-gnueabihf-gcc
CXX = arm-buildroot-linux-gnueabihf-g++

# ==========================================================================
# 编译选项
# ==========================================================================

# 头文件路径
INCLUDE_PATH = -I include/test/ \
               -I include/common/ \
               -I include/mqttclient \
               -I include/ \
               -I include/common/log \
               -I include/mqtt \
               -I ./include/platform/linux \
               -I ./include/network

# 库文件路径
LIB_PATH = -L .

# ==========================================================================
# 源文件
# ==========================================================================

# C++源文件
CPP_SRCS = mqtt_bridge.cpp rpc_client.cpp cJSON.cpp

# C源文件
C_SRCS = config.c log.c error.c watchdog.c data_cache.c

# 目标文件
CPP_OBJS = $(CPP_SRCS:.cpp=.o)
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(CPP_OBJS) $(C_OBJS)

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
LDFLAGS = $(LIB_PATH) -lmqttclient -lpthread

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
# 帮助信息
# ==========================================================================

help:
	@echo "Available targets:"
	@echo "  all     - Build with Release mode (default)"
	@echo "  debug   - Build with Debug mode"
	@echo "  release - Build with Release mode"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - Show installation instructions"
	@echo "  help    - Show this help message"

# ==========================================================================
# 依赖关系（自动依赖生成）
# ==========================================================================

# 如果需要自动依赖生成，可以取消注释以下行
# -include $(OBJS:.o=.d)

.PHONY: all debug release clean install help
