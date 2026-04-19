cmake_minimum_required(VERSION 3.10)
project(mini_webserver)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

if(WIN32)
    file(GLOB SRC "${CMAKE_CURRENT_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
else()
    file(GLOB SRC "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
endif()

add_executable(server ${SRC} "include/event_loop.h")

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()
