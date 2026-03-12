cmake_minimum_required(VERSION 3.10)
project(xdoj)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/languageRunners)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sandboxRuntime)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/scheduler)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/service)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/workerPools)

# 收集源文件
file(GLOB_RECURSE ALL_SOURCE_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# 排除测试文件
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*SandboxTestMain\\.cpp$")

# 创建库
add_library(codesandbox STATIC ${ALL_SOURCE_FILES})

# 链接库
target_link_libraries(codesandbox 
    jsoncpp 
    pthread 
    seccomp
)

# 创建测试程序
add_executable(sandbox_test ${CMAKE_CURRENT_SOURCE_DIR}/SandboxTestMain.cpp)
target_link_libraries(sandbox_test codesandbox jsoncpp pthread seccomp)

# 编译选项
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -std=c++17")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -g -Wall -std=c++17")
