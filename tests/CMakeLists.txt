add_executable(test_log_parser test_log_parser.c ../src/log_parser.c)
target_include_directories(test_log_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_log_parser PRIVATE Threads::Threads rt)
add_test(NAME test_log_parser COMMAND test_log_parser)

add_executable(test_file_writer test_file_writer.c ../src/file_writer.c)
target_include_directories(test_file_writer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_file_writer PRIVATE Threads::Threads rt)
add_test(NAME test_file_writer COMMAND test_file_writer)

add_executable(test_shm_buffer test_shm_buffer.c ../src/shm_buffer.c)
target_include_directories(test_shm_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_shm_buffer PRIVATE Threads::Threads rt)
add_test(NAME test_shm_buffer COMMAND test_shm_buffer)

add_executable(test_signal_handler test_signal_handler.c ../src/signal_handler.c)
target_include_directories(test_signal_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_signal_handler PRIVATE Threads::Threads rt)
add_test(NAME test_signal_handler COMMAND test_signal_handler)

add_executable(test_daemon test_daemon.c ../src/daemon.c)
target_include_directories(test_daemon PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_daemon PRIVATE ${SYSTEMD_LIBRARIES})
add_test(NAME test_daemon COMMAND test_daemon)

add_executable(test_config test_config.c)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME test_config COMMAND test_config)
