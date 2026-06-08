# 共享库Makefile
# 编译公共模块为静态库，消除代码重复
# 使用ARM交叉编译工具链，与lesson5/lesson6保持一致

CC = arm-buildroot-linux-gnueabihf-gcc
CXX = arm-buildroot-linux-gnueabihf-g++
AR = arm-buildroot-linux-gnueabihf-ar

# 编译选项
CFLAGS = -Wall -Wextra -O2 -fPIC
CXXFLAGS = -Wall -Wextra -O2 -fPIC

# 目标静态库
STATIC_LIB = libshared.a

# 目录
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# 公共模块源文件（只含实际重复的模块）
C_SRCS = $(SRC_DIR)/cJSON.c \
         $(SRC_DIR)/watchdog.c

# 目标文件
C_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
OBJS = $(C_OBJS)

# 头文件路径
INCLUDES = -I$(INC_DIR)

# 默认目标
all: $(BUILD_DIR) $(STATIC_LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(STATIC_LIB): $(OBJS)
	$(AR) rcs $@ $^
	@echo "=== 共享库编译完成: $(STATIC_LIB) ==="

# 编译测试（用host gcc，仅语法检查）
test: CFLAGS += -DTEST_MAIN
test: $(BUILD_DIR) $(C_OBJS)

clean:
	rm -rf $(BUILD_DIR) $(STATIC_LIB)

.PHONY: all clean test
