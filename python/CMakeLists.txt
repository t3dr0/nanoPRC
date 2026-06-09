cmake_minimum_required(VERSION 3.20)
project(nanoprc_py LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# This scaffold links against an already-built nano_prc shared library from the
# main project build tree. Build nanoPRC first in ../build.
find_library(NANOPRC_LIBRARY
    NAMES nano_prc
    PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/../build/Release"
    NO_DEFAULT_PATH
)

if(NOT NANOPRC_LIBRARY)
    message(FATAL_ERROR
        "Could not find nano_prc library. Build the main project first (cmake -S .. -B ../build) "
        "or update NANOPRC_LIBRARY search paths in python/CMakeLists.txt.")
endif()

pybind11_add_module(_core MODULE src/bindings.cpp)
target_include_directories(_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(_core PRIVATE ${NANOPRC_LIBRARY})

install(TARGETS _core LIBRARY DESTINATION nanoprc_py)
