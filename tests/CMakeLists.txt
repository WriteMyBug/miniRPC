add_executable(buffer_test buffer_test.cpp)
target_link_libraries(buffer_test PRIVATE minirpc)
add_test(NAME buffer_test COMMAND buffer_test)

