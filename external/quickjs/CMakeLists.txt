cmake_minimum_required(VERSION 3.18)
project(quickjs C)

set(QUICKJS_SOURCES
    quickjs.c
    libregexp.c
    libunicode.c
    cutils.c
    quickjs-libc.c
    libbf.c
)

add_library(quickjs STATIC ${QUICKJS_SOURCES})

target_compile_definitions(quickjs PRIVATE
    _GNU_SOURCE
    CONFIG_VERSION="2024-01-13"
    CONFIG_BIGNUM
)

# 抑制第三方 C 源码的编译警告（如 cast-function-type-mismatch 等），保持项目构建 0 警告
if (MSVC)
    target_compile_options(quickjs PRIVATE /W0)
else ()
    target_compile_options(quickjs PRIVATE -w)
endif ()

target_include_directories(quickjs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
