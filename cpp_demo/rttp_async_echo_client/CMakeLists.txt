cmake_minimum_required(VERSION 3.10)

#set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../bin")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
#link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS})

include_directories(../../api)

add_executable(RttpAsyncEchoClient rttp_async_echo_client.cpp main.cpp)


IF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

    target_link_libraries(RttpAsyncEchoClient ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/RttpDLL.lib  ws2_32.lib)
    
ELSE(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
    
    IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        include_directories(/usr/local/include)
        target_link_libraries(RttpAsyncEchoClient ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.dylib)
    ELSE(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_link_libraries(RttpAsyncEchoClient ${CMAKE_CURRENT_SOURCE_DIR}/../../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.so)
    ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

