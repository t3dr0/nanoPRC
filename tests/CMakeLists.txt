# Test binaries are registered here as they are added in later phases.
# Each test is a standalone executable using tests/prc_test.h (see that
# header for the assertion macros), registered as a CTest test.
#
# Pattern for adding a new test:
#
#   add_executable(test_NAME tests/unit/test_NAME.c)
#   target_link_libraries(test_NAME prc_static)
#   add_test(NAME test_NAME COMMAND test_NAME)
