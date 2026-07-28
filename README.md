# log_kit

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-passing-green.svg)](#构建)

轻量级 C++ 日志库，支持模块独立级别控制、输出重定向和 Unix Domain Socket 远程控制。

## ✨ 特性

- **Logger 类封装** — 模块注册后获得 Logger 对象，封装身份（ID、名称）和操作
- **模块独立级别** — 每个 Logger 独立控制日志级别
- **按名查找** — `LogFindModule(name)` 可通过名称找回已注册模块
- **输出重定向** — 每个模块可独立重定向到文件
- **线程安全** — 基于 `pthread_mutex` 和 `localtime_r` 实现
- **Socket 远程控制** — 运行时通过 Unix Domain Socket 调整日志级别
- **log_tool 工具** — 命令行配置工具，无需重新编译即可调整日志级别
- **RAII 守卫** — `LockGuard`/`FdGuard` 管理锁和文件描述符生命周期
- **协议分离** — 命令协议独立为 `log_cmd.h`，客户端和服务端共享
- **宏带 _ 前缀** — `_LOG_*` 宏避免与业务代码冲突

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

// 1. 注册模块，获得 Logger 对象（自动去重，多次注册返回同一 Logger）
log_kit::Logger net = log_kit::LogRegister("network");
log_kit::Logger codec = log_kit::LogRegister("codec");

// 2. 设置日志级别（可选，默认为 kInfo）
net.SetLevel(log_kit::LogLevel::kDebug);
codec.SetLevel(log_kit::LogLevel::kWarn);

// 3. 打印日志（传入 Logger 对象）
_LOG_TRACE(net, "详细跟踪信息");
_LOG_DEBUG(net, "调试信息, count=%d", count);
_LOG_INFO(net, "一般信息: %s", msg);
_LOG_WARN(codec, "警告信息");
_LOG_ERROR(codec, "错误: %s", err);
_LOG_FATAL(net, "致命错误");
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
net.SetOutputFile("/var/log/my-app.log");

// 恢复输出到 stderr
net.ResetOutput();
```

### 按名查找

```cpp
// 从其他位置找回已注册的模块
log_kit::Logger net = log_kit::LogFindModule("network");
if (net.IsValid()) {
    net.SetLevel(log_kit::LogLevel::kTrace);
}
```

### 适配器模式

在子库中使用时，推荐创建适配器头文件自动注册：

```cpp
// mylib/src/util/log.h
#ifndef MYLIB_LOG_H_
#define MYLIB_LOG_H_
#include "log_kit.h"

namespace mylib {
inline log_kit::Logger GetLogger() {
    static log_kit::Logger logger = log_kit::LogRegister("mylib");
    return logger;
}
}  // namespace mylib

#define MYLIB_LOG_INFO(fmt, ...) \
    _LOG_INFO(::mylib::GetLogger(), fmt, ##__VA_ARGS__)
#endif
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


## 📖 API 参考

### Logger 类

| 方法 | 说明 |
|------|------|
| `Logger()` | 默认构造，无效句柄（IsValid() == false） |
| `Logger::IsValid()` | 是否为有效模块 |
| `Logger::Id()` | 获取模块 ID（>=1） |
| `Logger::GetName()` | 获取模块名称 |
| `Logger::GetLevel()` | 获取当前日志级别 |
| `Logger::SetLevel(level)` | 设置日志级别 |
| `Logger::SetOutputFile(path)` | 重定向输出到文件 |
| `Logger::ResetOutput()` | 恢复输出到 stderr |

### 核心 API

| 函数 | 说明 |
|------|------|
| `LogRegister(name)` | 注册模块，返回 Logger 对象（自动去重） |
| `LogFindModule(name)` | 按名称查找已注册模块 |
| `LogSetLevel(module_id, level)` | 按 ID 设置级别，ID=0 表示所有模块 |
| `LogGetLevel(module_id)` | 按 ID 获取级别 |
| `LogSetOutputFile(module_id, path)` | 按 ID 重定向输出 |
| `LogResetOutput(module_id)` | 按 ID 恢复输出 |
| `LogGetModuleCount()` | 获取已注册模块数量 |
| `LogGetModuleName(module_id)` | 按 ID 获取模块名称 |

### Socket API

| 函数 | 说明 |
|------|------|
| `LogStartSocket(socket_path)` | 启动 Socket 控制服务器 |
| `LogStopSocket()` | 停止 Socket 控制服务器 |

### 日志宏（带 _ 前缀，避免冲突）

| 宏 | 说明 |
|----|------|
| `_LOG_TRACE(logger, fmt, ...)` | TRACE 级别日志 |
| `_LOG_DEBUG(logger, fmt, ...)` | DEBUG 级别日志 |
| `_LOG_INFO(logger, fmt, ...)` | INFO 级别日志 |
| `_LOG_WARN(logger, fmt, ...)` | WARN 级别日志 |
| `_LOG_ERROR(logger, fmt, ...)` | ERROR 级别日志 |
| `_LOG_FATAL(logger, fmt, ...)` | FATAL 级别日志 |

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
