# log_kit

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-passing-green.svg)](#构建)

轻量级 C++ 日志库，支持模块独立级别控制、输出重定向和 Unix Domain Socket 远程控制。

## ✨ 特性

- **模块注册机制** — 每个模块独立控制日志级别
- **模块 ID 自增** — 从 1 开始，ID 0 保留为批量操作所有模块
- **输出重定向** — 每个模块可独立重定向到文件
- **线程安全** — 基于 `pthread_mutex` 和 `localtime_r` 实现
- **Socket 远程控制** — 运行时通过 Unix Domain Socket 调整日志级别
- **log_tool 工具** — 命令行配置工具，无需重新编译即可调整日志级别
- **RAII 守卫** — `LockGuard`/`FdGuard` 管理锁和文件描述符生命周期
- **协议分离** — 命令协议独立为 `log_cmd.h`，客户端和服务端共享

## 📦 目录结构

```
log-kit/
├── CMakeLists.txt              # CMake 构建配置
├── include/
│   └── log_kit.h               # 公共 API 头文件
├── src/
│   ├── log_kit.cc              # 核心实现（日志写入、SocketServer）
│   ├── log_tool.cc             # log_tool 命令行工具
│   ├── log_config.h            # 内部常量定义（模块上限、缓冲区大小、路径）
│   ├── log_guard.h             # RAII 守卫（LockGuard、FdGuard）
│   ├── log_cmd.h               # 命令协议（枚举、命令表、构建函数）
│   └── log_internal.h          # 内部核心结构（ModuleInfo、LogContext、SocketServer）
├── demo/
│   └── main.cc                 # 使用示例
├── cmake/
│   ├── version.h.in            # 版本头文件模板
│   └── log_kit.pc.in           # pkg-config 模板
├── LICENSE                     # MIT License
└── README.md
```

## 🔧 安装与构建

### 依赖

- C++11 兼容的编译器 (GCC >= 4.8, Clang >= 3.4)
- CMake >= 3.10
- pthread (POSIX threads)

### 快速开始

```bash
# 克隆仓库
git clone git@github.com:zqiomi/log-kit.git
cd log-kit

# 创建构建目录
mkdir build && cd build

# 配置（默认启用所有功能）
cmake ..

# 编译
make -j$(nproc)

# 安装（可选）
sudo make install
```

### 构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_SHARED_LIBS` | ON | 构建动态库 |
| `BUILD_STATIC_LIBS` | ON | 构建静态库 |
| `BUILD_DEMO` | ON | 构建示例程序和 log_tool |
| `LOG_KIT_ENABLE_SOCKET` | ON | 启用 Unix Domain Socket 控制服务器 |
| `LOG_KIT_DEFAULT_SOCKET_PATH` | `/tmp/log_kit.sock` | 默认 Socket 文件路径 |

**示例：禁用 Socket 支持**

```bash
cmake .. -DLOG_KIT_ENABLE_SOCKET=OFF
```

## 📝 使用方法

### 基本用法

```cpp
#include "log_kit.h"

// 1. 注册模块（在初始化时调用一次）
static const int kModuleId = log_kit::LogRegister("my-module");

// 2. 设置日志级别（可选，默认为 kInfo）
log_kit::LogSetLevel(kModuleId, log_kit::LogLevel::kDebug);

// 3. 打印日志
LOG_TRACE(kModuleId, "详细跟踪信息");
LOG_DEBUG(kModuleId, "调试信息, count=%d", count);
LOG_INFO(kModuleId, "一般信息: %s", msg);
LOG_WARN(kModuleId, "警告信息");
LOG_ERROR(kModuleId, "错误: %s", err);
LOG_FATAL(kModuleId, "致命错误");
```

### 日志级别

| 级别 | 数值 | 说明 |
|------|------|------|
| `kTrace` | 0 | 详细跟踪（最详细） |
| `kDebug` | 1 | 调试信息 |
| `kInfo` | 2 | 一般信息（默认） |
| `kWarn` | 3 | 警告 |
| `kError` | 4 | 错误 |
| `kFatal` | 5 | 致命错误（最少） |

### 输出重定向

```cpp
// 将模块输出重定向到文件
log_kit::LogSetOutputFile(kModuleId, "/var/log/my-app.log");

// 恢复输出到 stderr
log_kit::LogSetOutputFile(kModuleId, nullptr);
// 或
log_kit::LogResetOutput(kModuleId);
```

### 批量操作

```cpp
// 模块 ID 为 0 时，操作所有模块
log_kit::LogSetLevel(0, log_kit::LogLevel::kWarn);  // 所有模块设为 WARN
```

## 🔌 远程控制（Socket）

### 启动 Socket 服务器

```cpp
// 在主程序启动时
log_kit::LogStartSocket();  // 使用默认路径
// 或指定路径
log_kit::LogStartSocket("/custom/path/log.sock");

// 退出时
log_kit::LogStopSocket();
```

### 使用 log_tool 客户端

`log_tool` 是独立的命令行工具，用于远程控制运行中程序的日志级别。

```bash
# 查看所有模块状态
./log_tool

# 设置指定模块级别
./log_tool level 1 trace      # 模块1 设为 TRACE
./log_tool level 2 debug      # 模块2 设为 DEBUG
./log_tool level 0 warn       # 所有模块设为 WARN

# 使用数字级别
./log_tool level 1 4          # 模块1 设为 ERROR (级别4)

# 指定 Socket 路径
./log_tool /custom/path/log.sock list

# 关闭目标程序的 Socket 服务器
./log_tool quit
```

**日志级别映射：**
- `0` = trace
- `1` = debug
- `2` = info
- `3` = warn
- `4` = error
- `5` = fatal

## 🏗️ 内部架构

### 头文件分层

```
log_kit.h          # 对外 API（用户 include 此文件即可）
  └── log_config.h     常量定义
  └── log_cmd.h        命令协议（客户端+服务端共享）

log_internal.h     # 内部核心（仅服务端使用）
  ├── log_config.h     常量
  ├── log_guard.h      RAII 守卫
  └── log_cmd.h        命令协议
```

### RAII 守卫

```cpp
#include "log_guard.h"

// 自动加锁解锁
pthread_mutex_t mtx;
{
    log_kit::LockGuard lock(mtx);
    // 临界区操作
}  // 自动解锁

// 自动关闭文件描述符
log_kit::FdGuard fd(open("file.log", O_WRONLY));
if (fd.IsValid()) {
    write(fd.fd(), data, len);
}  // 自动 close(fd)
```

### 协议扩展

新增命令只需三步：

```cpp
// 1. 在 log_cmd.h 的 kCmdTable 添加一行
{"flush", CmdType::kFlush},

// 2. 在 CmdType 枚举添加 kFlush
// 3. 在 log_kit.cc 的 HandleCommand switch 添加 case
case CmdType::kFlush:
    // 实现
    break;
```

## 📖 API 参考

### 核心 API

| 函数 | 说明 |
|------|------|
| `LogRegister(name)` | 注册模块，返回模块 ID（>=1） |
| `LogSetLevel(module_id, level)` | 设置模块日志级别，ID=0 表示所有模块 |
| `LogGetLevel(module_id)` | 获取模块当前日志级别 |
| `LogSetOutputFile(module_id, path)` | 重定向输出到文件，nullptr 恢复 stderr |
| `LogResetOutput(module_id)` | 恢复模块输出到 stderr |
| `LogGetModuleCount()` | 获取已注册模块数量 |
| `LogGetModuleName(module_id)` | 获取模块名称 |

### Socket API

| 函数 | 说明 |
|------|------|
| `LogStartSocket(socket_path)` | 启动 Socket 控制服务器 |
| `LogStopSocket()` | 停止 Socket 控制服务器 |

### 日志宏

| 宏 | 说明 |
|----|------|
| `LOG_TRACE(id, fmt, ...)` | TRACE 级别日志 |
| `LOG_DEBUG(id, fmt, ...)` | DEBUG 级别日志 |
| `LOG_INFO(id, fmt, ...)` | INFO 级别日志 |
| `LOG_WARN(id, fmt, ...)` | WARN 级别日志 |
| `LOG_ERROR(id, fmt, ...)` | ERROR 级别日志 |
| `LOG_FATAL(id, fmt, ...)` | FATAL 级别日志 |

## 📤 输出格式

```
[2026-07-28 21:36:09] [DEBUG] [main.cc:main:100] demo 心跳, count=0
[时间戳] [级别] [文件:函数:行号] 日志内容
```

## 🔄 与 CMake 集成

```cmake
# 在你的项目中使用 log_kit
find_package(log_kit REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app log_kit)
```

或者直接子目录引入：

```cmake
add_subdirectory(log-kit)
target_link_libraries(my_app log_kit_shared)
```

## 📄 License

本项目基于 [MIT License](LICENSE) 开源。

---

**Author**: zqiomi (zqiomi@gmail.com)