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
    "include/buffer.h"
    "src/buffer.cpp"
    "include/http_handler.h"
    "src/http_handler.cpp")

target_include_directories(server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(WIN32)
    target_link_libraries(server PRIVATE ws2_32)
endif()

if(MSVC AND WIN32)
    get_filename_component(_link_dir "${CMAKE_LINKER}" DIRECTORY)
    get_filename_component(_link_dir "${_link_dir}" DIRECTORY)
    get_filename_component(_link_dir "${_link_dir}" DIRECTORY)
    get_filename_component(_vctools_root "${_link_dir}" DIRECTORY)

    get_filename_component(_mt_dir "${CMAKE_MT}" DIRECTORY)
    get_filename_component(_mt_dir "${_mt_dir}" DIRECTORY)
    get_filename_component(_sdk_version "${_mt_dir}" NAME)
    get_filename_component(_sdk_bin_root "${_mt_dir}" DIRECTORY)
    get_filename_component(_sdk_root "${_sdk_bin_root}" DIRECTORY)

    target_link_options(server PRIVATE
        "/LIBPATH:${_vctools_root}/lib/x64"
        "/LIBPATH:${_sdk_root}/lib/${_sdk_version}/um/x64"
        "/LIBPATH:${_sdk_root}/lib/${_sdk_version}/ucrt/x64"
    )
endif()
