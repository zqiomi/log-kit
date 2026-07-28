cmake_minimum_required(VERSION 3.10)
project(log_kit
    VERSION 1.0.0
    DESCRIPTION "轻量级C++日志库，支持模块独立级别控制、输出重定向和Socket远程控制"
    LANGUAGES CXX)

# ===== 编译选项 =====
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "-Wall -Wextra -Werror -fno-exceptions -g -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Werror -fno-exceptions -O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Wall -Wextra -Werror -fno-exceptions -O2 -g -DNDEBUG")

# ===== 构建选项 =====
option(BUILD_SHARED_LIBS "构建动态库" ON)
option(BUILD_STATIC_LIBS "构建静态库" ON)
option(BUILD_DEMO "构建示例程序" ON)
option(LOG_KIT_ENABLE_SOCKET "启用 Unix Domain Socket 控制服务器" ON)
set(LOG_KIT_DEFAULT_SOCKET_PATH "/tmp/log_kit.sock" CACHE STRING "默认 Socket 文件路径")

# 标准安装路径
include(GNUInstallDirs)

# 头文件路径
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# ===== 版本头文件 =====
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    @ONLY
)

# ===== 源文件（按目录组织）=====
set(LOG_KIT_SOURCES
    src/core/log_kit_impl.cc
    src/api/log_kit.cc
    src/core/log_server.cc
)

# ===== Socket 支持 =====
if(LOG_KIT_ENABLE_SOCKET)
    find_package(Threads REQUIRED)
    add_definitions(-DLOG_KIT_DEFAULT_SOCKET_PATH="${LOG_KIT_DEFAULT_SOCKET_PATH}")
endif()

# ===== 动态库 =====
if(BUILD_SHARED_LIBS)
    add_library(log_kit_shared SHARED ${LOG_KIT_SOURCES})
    set_target_properties(log_kit_shared PROPERTIES
        OUTPUT_NAME log_kit
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    target_include_directories(log_kit_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if(LOG_KIT_ENABLE_SOCKET)
        target_link_libraries(log_kit_shared PUBLIC Threads::Threads)
    endif()
    install(TARGETS log_kit_shared
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# ===== 静态库 =====
if(BUILD_STATIC_LIBS)
    add_library(log_kit_static STATIC ${LOG_KIT_SOURCES})
    set_target_properties(log_kit_static PROPERTIES
        OUTPUT_NAME log_kit
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(log_kit_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if(LOG_KIT_ENABLE_SOCKET)
        target_link_libraries(log_kit_static PUBLIC Threads::Threads)
    endif()
    install(TARGETS log_kit_static
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# ===== 安装头文件 =====
install(FILES include/log_kit.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ===== pkg-config =====
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/log_kit.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/log_kit.pc"
    @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/log_kit.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# ===== Demo 程序 =====
if(BUILD_DEMO)
    # 主 demo：展示日志功能 + Socket 实时控制
    add_executable(log_kit_demo demo/main.cc)
    if(BUILD_SHARED_LIBS)
        target_link_libraries(log_kit_demo PRIVATE log_kit_shared)
    else()
        target_link_libraries(log_kit_demo PRIVATE log_kit_static)
    endif()
    install(TARGETS log_kit_demo
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # log_tool：运行时日志级别配置工具
    add_executable(log_tool src/core/log_tool.cc)
    if(BUILD_SHARED_LIBS)
        target_link_libraries(log_tool PRIVATE log_kit_shared)
    else()
        target_link_libraries(log_tool PRIVATE log_kit_static)
    endif()
    install(TARGETS log_tool
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ===== CPack =====
set(CPACK_PACKAGE_NAME "log_kit")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "zqiomi@gmail.com")
set(CPACK_PACKAGE_VENDOR "log_kit")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
include(CPack)

# ===== 构建摘要 =====
message(STATUS "========== log_kit ${PROJECT_VERSION} ==========")
message(STATUS "动态库: ${BUILD_SHARED_LIBS}")
message(STATUS "静态库: ${BUILD_STATIC_LIBS}")
message(STATUS "示例程序: ${BUILD_DEMO}")
message(STATUS "Socket控制: ${LOG_KIT_ENABLE_SOCKET}")
if(LOG_KIT_ENABLE_SOCKET)
    message(STATUS "默认Socket路径: ${LOG_KIT_DEFAULT_SOCKET_PATH}")
endif()
message(STATUS "==========================================")
