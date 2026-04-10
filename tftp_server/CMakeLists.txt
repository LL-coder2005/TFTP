cmake_minimum_required(VERSION 3.16)
project(tftp_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tftp_server
    main.cpp
    tftp_server.cpp
)

# 如果你的代码里用到了 pthread（比如 std::thread 或 pthread_*），打开下面三行
# find_package(Threads REQUIRED)
# target_link_libraries(tftp_server PRIVATE Threads::Threads)

# 可选：打开更多警告，方便查错
target_compile_options(tftp_server PRIVATE -Wall -Wextra -Wpedantic)