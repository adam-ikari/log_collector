cmake_minimum_required(VERSION 3.10)
project(log_collector C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)

# Source files to be added in subsequent tasks:
#   src/config.c
#   src/shm_buffer.c
#   src/log_parser.c
#   src/file_writer.c
#   src/signal_handler.c
#   src/daemon.c
#   src/master.c
#   src/worker.c

add_executable(log_collector
    src/main.c
    src/config.c
    src/daemon.c
    src/file_writer.c
    src/log_parser.c
    src/master.c
    src/shm_buffer.c
    src/signal_handler.c
    src/worker.c
)

target_include_directories(log_collector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(log_collector PRIVATE
    Threads::Threads
    rt
)

# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS log_collector RUNTIME DESTINATION sbin)
install(FILES conf/log-collector.conf.example DESTINATION /etc)
