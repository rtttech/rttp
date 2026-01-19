cmake_minimum_required(VERSION 3.10)

project(RttpProject VERSION 1.0)

add_subdirectory(third_party/libuv-1.51.0)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/libuv-1.51.0/include)

add_subdirectory(cpp_demo/rttp_async_echo_client)
add_subdirectory(cpp_demo/rttp_async_echo_server)
add_subdirectory(cpp_demo/rttp_stub_demo)

add_subdirectory(rttp_proxy)
