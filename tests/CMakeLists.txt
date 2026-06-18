add_executable(test_config test_config.c ../src/config.c)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_config PRIVATE Threads::Threads rt)
add_test(NAME test_config COMMAND test_config)

add_executable(test_log_parser test_log_parser.c ../src/log_parser.c)
target_include_directories(test_log_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_log_parser PRIVATE Threads::Threads rt)
add_test(NAME test_log_parser COMMAND test_log_parser)
