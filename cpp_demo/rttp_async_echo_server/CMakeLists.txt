cmake_minimum_required(VERSION 3.10)

#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../bin")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
#link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS})

include_directories(../../api)

add_executable(RttpAsyncEchoServer rttp_async_echo_server.cpp main.cpp)


IF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
    target_link_libraries(RttpAsyncEchoServer ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/RttpDLL.lib  ws2_32.lib)
    
ELSE(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")

    IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        include_directories(/usr/local/include)
        target_link_libraries(RttpAsyncEchoServer ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.dylib)
    ELSE(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_link_libraries(RttpAsyncEchoServer ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.so)
    ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
 
