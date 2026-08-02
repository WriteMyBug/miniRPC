cmake_minimum_required(VERSION 3.16)
project(minirpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# protobuf（第 2 周起默认开启，依赖 protobuf-compiler / libprotobuf-dev）。
option(MINIRPC_ENABLE_PROTOBUF "Enable Protocol Buffers codec" ON)

add_library(minirpc
  src/common/Logger.cpp
  src/codec/Codec.cpp
  src/net/Buffer.cpp
  src/net/Channel.cpp
  src/net/EpollPoller.cpp
  src/net/EventLoop.cpp
  src/net/InetAddress.cpp
  src/net/Socket.cpp
  src/net/TcpConnection.cpp
  src/net/TcpServer.cpp
  src/thread/ThreadPool.cpp
)
target_include_directories(minirpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(minirpc PRIVATE -Wall -Wextra -Wpedantic)

if(MINIRPC_ENABLE_PROTOBUF)
  find_package(Protobuf REQUIRED)
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    proto/echo.proto
    proto/calculator.proto)
  # protoc 生成代码不受我们编译选项约束
  set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
  target_sources(minirpc PRIVATE ${PROTO_SRCS})
  target_include_directories(minirpc PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(minirpc PUBLIC protobuf::libprotobuf)
  target_compile_definitions(minirpc PUBLIC MINIRPC_ENABLE_PROTOBUF=1)
endif()

add_subdirectory(examples)
enable_testing()
add_subdirectory(tests)
