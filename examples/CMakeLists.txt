add_executable(echo_server echo/echo_server.cpp)
target_link_libraries(echo_server PRIVATE minirpc)

add_executable(echo_client echo/echo_client.cpp)
target_link_libraries(echo_client PRIVATE minirpc)

