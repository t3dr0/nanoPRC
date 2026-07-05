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
