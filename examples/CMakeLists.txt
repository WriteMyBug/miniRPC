add_executable(echo_server echo/echo_server.cpp)
target_link_libraries(echo_server PRIVATE minirpc)

add_executable(echo_client echo/echo_client.cpp)
target_link_libraries(echo_client PRIVATE minirpc)

add_executable(echo_stress echo/echo_stress.cpp)
target_link_libraries(echo_stress PRIVATE minirpc)

add_executable(calculator_server rpc/calculator_server.cpp)
target_link_libraries(calculator_server PRIVATE minirpc)

add_executable(calculator_client rpc/calculator_client.cpp)
target_link_libraries(calculator_client PRIVATE minirpc)

add_executable(rpc_node rpc/rpc_node.cpp)
target_link_libraries(rpc_node PRIVATE minirpc)

add_executable(lb_client rpc/lb_client.cpp)
target_link_libraries(lb_client PRIVATE minirpc)
