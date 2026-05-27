cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(zlib)

include(CheckIncludeFile)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(stdarg.h HAVE_STDARG_H)

if (HAVE_UNISTD_H)
    add_definitions(-DHAVE_UNISTD_H)
endif ()

if (HAVE_STDARG_H)
    add_definitions(-DHAVE_STDARG_H)
endif ()

set(ZLIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/zlib)

# zlib sources
add_library(zlib STATIC
    ${ZLIB_ROOT}/adler32.c
    ${ZLIB_ROOT}/compress.c
    ${ZLIB_ROOT}/crc32.c
    ${ZLIB_ROOT}/deflate.c
    ${ZLIB_ROOT}/gzclose.c
    ${ZLIB_ROOT}/gzlib.c
    ${ZLIB_ROOT}/gzread.c
    ${ZLIB_ROOT}/gzwrite.c
    ${ZLIB_ROOT}/infback.c
    ${ZLIB_ROOT}/inffast.c
    ${ZLIB_ROOT}/inflate.c
    ${ZLIB_ROOT}/inftrees.c
    ${ZLIB_ROOT}/trees.c
    ${ZLIB_ROOT}/uncompr.c
    ${ZLIB_ROOT}/zutil.c)

set_property(TARGET zlib PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(zlib PUBLIC ${ZLIB_ROOT})
