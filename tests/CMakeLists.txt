# Test binaries are registered here as they are added in later phases.
# Each test is a standalone executable using tests/prc_test.h (see that
# header for the assertion macros), registered as a CTest test.
#
# Pattern for adding a new test:
#
#   add_executable(test_NAME tests/unit/test_NAME.c)
#   target_link_libraries(test_NAME prc_static)
#   target_include_directories(test_NAME PRIVATE tests)
#   add_test(NAME test_NAME COMMAND test_NAME)

add_executable(test_bitwrite tests/unit/test_bitwrite.c)
target_link_libraries(test_bitwrite prc_static)
target_include_directories(test_bitwrite PRIVATE tests)
add_test(NAME test_bitwrite COMMAND test_bitwrite)

add_executable(test_compress_tess tests/unit/test_compress_tess.c)
target_link_libraries(test_compress_tess prc_static)
target_include_directories(test_compress_tess PRIVATE tests)
add_test(NAME test_compress_tess COMMAND test_compress_tess)

add_executable(test_traversal tests/unit/test_traversal.c)
target_link_libraries(test_traversal prc_static)
target_include_directories(test_traversal PRIVATE tests)
add_test(NAME test_traversal COMMAND test_traversal)

add_executable(test_normals tests/unit/test_normals.c)
target_link_libraries(test_normals prc_static)
target_include_directories(test_normals PRIVATE tests)
add_test(NAME test_normals COMMAND test_normals)

add_executable(test_tess_3d tests/unit/test_tess_3d.c)
target_link_libraries(test_tess_3d prc_static)
target_include_directories(test_tess_3d PRIVATE tests)
add_test(NAME test_tess_3d COMMAND test_tess_3d)

add_executable(test_wire_tess tests/unit/test_wire_tess.c)
target_link_libraries(test_wire_tess prc_static)
target_include_directories(test_wire_tess PRIVATE tests)
add_test(NAME test_wire_tess COMMAND test_wire_tess)
