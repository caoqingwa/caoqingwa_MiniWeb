cmake_minimum_required(VERSION 3.10)
project(mini_webserver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    file(GLOB PLATFORM_SRC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/win/*.cpp")
else()
    file(GLOB PLATFORM_SRC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/*.cpp")
endif()

file(GLOB CORE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(server
    ${CORE_SRC}
    ${PLATFORM_SRC}
)

target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()
