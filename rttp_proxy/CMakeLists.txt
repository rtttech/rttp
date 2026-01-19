cmake_minimum_required(VERSION 3.10)

# Set project name
project(RttpProxyServer VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")

# Enable build directories
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# libuv has been added in the top-level CMakeLists.txt

include_directories(../api)

# Set source files
set(SERVER_SOURCES
    server/server_main.cpp
    server/rttp_server.cpp
    server/rttp_tcp_bridge.cpp
    server/stats_manager.cpp
    server/http_monitor.cpp
    server/http_parser.cpp
)

# Set source files
set(CLIENT_SOURCES
    client/client_main.cpp
    client/tcp_server.cpp
    client/tcp_rttp_bridge.cpp
)


# Generate executable
add_executable(RttpProxyServer ${SERVER_SOURCES})

# Generate executable
add_executable(RttpProxyClient ${CLIENT_SOURCES})

# Link Boost libraries and libuv
target_link_libraries(RttpProxyServer PRIVATE
    ${Boost_LIBRARIES}
)

# Link libuv from third_party
target_link_libraries(RttpProxyServer PRIVATE uv_a)
target_link_libraries(RttpProxyClient PRIVATE uv_a)

# Platform-specific library links
if(WIN32)
    # Windows needs additional system libraries for libuv
    target_link_libraries(RttpProxyServer PRIVATE 
        ws2_32 
        iphlpapi 
        psapi 
        userenv 
        ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/RttpDLL.lib
    )
    target_link_libraries(RttpProxyClient PRIVATE 
        ws2_32 
        ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/RttpDLL.lib
    )

else()
    IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        include_directories(/usr/local/include)
        target_link_libraries(RttpProxyServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.dylib pthread)
        target_link_libraries(RttpProxyClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.dylib pthread)
    ELSE(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        # Linux
        target_link_libraries(RttpProxyServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.so pthread)
        target_link_libraries(RttpProxyClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../api/${CMAKE_SYSTEM_NAME}${BITS}/libRttpDLL.so pthread)
    ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

    
    
endif()

# Set output directory
set_target_properties(RttpProxyServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
)

set_target_properties(RttpProxyClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
)

# Output build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Project version: ${PROJECT_VERSION}")
message(STATUS "Using C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")