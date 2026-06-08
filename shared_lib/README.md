# 共享库架构

## 目的

消除项目中的代码重复问题，将公共模块集中管理。

## 问题分析

当前项目中以下模块存在重复副本：

| 模块 | 副本位置 | 说明 |
|------|----------|------|
| cJSON | lesson5/, lesson6/, web_admin/ | JSON解析库 |
| rpc_client | lesson5/, lesson6/ | RPC客户端 |
| watchdog | lesson5/, lesson6/ | 看门狗模块 |

## 解决方案

创建共享库 `libshared.so` 和静态库 `libshared.a`，包含所有公共模块。

## 目录结构

```
shared_lib/
├── Makefile          # 构建脚本
├── README.md         # 本文件
├── include/          # 头文件目录
│   ├── cJSON.h
│   ├── rpc_client.h
│   ├── watchdog.h
│   ├── error.h
│   └── log.h
├── src/              # 源文件目录
│   ├── cJSON.c
│   ├── rpc_client.cpp
│   ├── watchdog.c
│   ├── error.c
│   └── log.c
└── build/            # 构建输出目录
```

## 使用方法

### 1. 编译共享库

```bash
cd shared_lib
make
```

### 2. 安装到系统

```bash
sudo make install
```

### 3. 在项目中使用

**方法一：链接共享库**
```bash
gcc -o myapp myapp.c -lshared -I/usr/local/include
```

**方法二：使用静态库**
```bash
gcc -o myapp myapp.c libshared.a -I/usr/local/include -lpthread
```

### 4. 修改现有项目的Makefile

```makefile
# 添加共享库路径
SHARED_LIB_DIR = ../shared_lib
SHARED_LIB = $(SHARED_LIB_DIR)/libshared.a

# 添加头文件包含路径
INCLUDES = -I$(SHARED_LIB_DIR)/include

# 链接共享库
LDFLAGS = $(SHARED_LIB) -lpthread
```

## 迁移步骤

### 第一阶段：准备共享库

1. ✅ 创建共享库目录结构
2. ✅ 复制公共模块源文件
3. ✅ 编写Makefile
4. ⬜ 测试编译

### 第二阶段：更新现有项目

1. ⬜ 修改lesson5/Makefile，使用共享库
2. ⬜ 修改lesson6/Makefile，使用共享库
3. ⬜ 修改web_admin/Makefile，使用共享库
4. ⬜ 删除重复的源文件副本

### 第三阶段：验证和清理

1. ⬜ 编译测试所有项目
2. ⬜ 运行单元测试
3. ⬜ 清理重复文件

## 优势

1. **代码维护**：只需维护一份公共代码
2. **版本一致性**：所有项目使用相同版本的库
3. **编译效率**：库只需编译一次
4. **存储空间**：减少重复文件占用

## 注意事项

1. 修改共享库后需要重新编译所有依赖项目
2. 保持API兼容性，避免破坏现有代码
3. 使用版本号管理库的更新
4. 在开发阶段建议使用静态库，便于调试

## 后续优化

1. 添加版本号管理
2. 支持动态加载（dlopen）
3. 添加单元测试
4. 集成到CI/CD流程
