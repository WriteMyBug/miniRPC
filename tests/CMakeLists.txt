add_executable(buffer_test buffer_test.cpp)
target_link_libraries(buffer_test PRIVATE minirpc)
add_test(NAME buffer_test COMMAND buffer_test)

add_executable(threadpool_test threadpool_test.cpp)
target_link_libraries(threadpool_test PRIVATE minirpc)
add_test(NAME threadpool_test COMMAND threadpool_test)

add_executable(codec_test codec_test.cpp)
target_link_libraries(codec_test PRIVATE minirpc)
add_test(NAME codec_test COMMAND codec_test)
