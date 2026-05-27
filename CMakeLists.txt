cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(nano_prc VERSION 0.1.0)

find_package(Git QUIET)

set(PRC_GIT_DESCRIBE "v${PROJECT_VERSION}")
if (GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always --match "v[0-9]*"
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE PRC_GIT_DESCRIBE
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET)
endif()

if (NOT PRC_GIT_DESCRIBE)
	set(PRC_GIT_DESCRIBE "v${PROJECT_VERSION}")
endif()

string(REGEX REPLACE "^v" "" PRC_VERSION_STRING "${PRC_GIT_DESCRIBE}")

configure_file(
	"${CMAKE_SOURCE_DIR}/include/prc_version.h.in"
	"${CMAKE_BINARY_DIR}/generated/prc_version.h"
	@ONLY)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

option(PRC_ENABLE_DEBUG_MEMORY "Enable PRC debug memory tracking" OFF)

add_subdirectory(thirdparty) # thirdparty libraries
add_subdirectory(demos) # demos

add_definitions(-DPRC_LITTLE_ENDIAN -DPRC_BUILD_SHARED)

# Our source files
add_library(nano_prc SHARED
	"src/prc_api.c"
	"src/prc_style_api.c"
	"src/prc_tri_primitives_api.c"
	"src/prc_tri_primitives_helper_api.c"
	"src/prc_uncompressed_primitives_api.c"
	"src/prc_bit.c"
	"src/prc_context.c"
	"src/prc_double.c"
	"src/prc_error.c"
	"src/prc_huff.c"
	"src/prc_memory.c"
	"src/prc_parse_common.c"
	"src/prc_parse_extra_geometry.c"
	"src/prc_parse_file_structure.c"
	"src/prc_parse_global.c"
	"src/prc_parse_main.c"
	"src/prc_parse_tess.c"
	"src/prc_parse_tree.c"
	"src/prc_pdf.c"
    "src/prc_pdf_dict.c"
	"src/prc_pdf_stream.c"
	"src/prc_pdf_decrypt.c"
	"src/prc_pdf_xref.c"
	"src/prc_release.c"
	"src/prc_schema.c"
	"src/prc_vector_util.c"
	"src/prc_decode_compressed_tess.c"
	"src/prc_decode_markup_tess.c")

# Specify C99 Standard
set_property(TARGET nano_prc PROPERTY C_STANDARD 99)

# Include directories
target_include_directories(nano_prc PUBLIC "include")

# Link zlib
target_link_libraries(nano_prc PRIVATE zlib)

# Link math library (Linux only)
if (UNIX)
	target_link_libraries(nano_prc PRIVATE m)
endif()

# Specify C99 Standard
set_property(TARGET nano_prc PROPERTY C_STANDARD 99)

target_include_directories(nano_prc PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(nano_prc PRIVATE PRC_VERSION_STRING="${PRC_VERSION_STRING}")
target_compile_definitions(nano_prc PUBLIC PRC_DEBUG_MEMORY=$<BOOL:${PRC_ENABLE_DEBUG_MEMORY}>)
# Install what we built
install(TARGETS nano_prc)
