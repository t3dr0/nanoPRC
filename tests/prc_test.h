/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PRC_TEST_H
#define PRC_TEST_H

/* Minimal C99 test harness: no heap allocations, no external dependencies.
   Each test binary is a single main() calling PRC_TEST_BEGIN/PRC_TEST_END
   around one or more PRC_ASSERT* checks. On success PRC_TEST_END returns 0
   from main(); on the first failed assertion, the failing macro prints the
   file/line/expression and exits 1 immediately. */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define PRC_TEST_BEGIN(test_name) \
    do { printf("running: %s\n", (test_name)); } while (0)

#define PRC_TEST_END \
    do { printf("PASS\n"); return 0; } while (0)

#define PRC_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: assertion failed: %s\n", \
                __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

#define PRC_ASSERT_EQ(a, b) \
    do { \
        long long prc_test_a_ = (long long)(a); \
        long long prc_test_b_ = (long long)(b); \
        if (prc_test_a_ != prc_test_b_) { \
            fprintf(stderr, "FAIL %s:%d: %s == %s failed (%lld != %lld)\n", \
                __FILE__, __LINE__, #a, #b, prc_test_a_, prc_test_b_); \
            exit(1); \
        } \
    } while (0)

#define PRC_ASSERT_NEAR(a, b, eps) \
    do { \
        double prc_test_a_ = (double)(a); \
        double prc_test_b_ = (double)(b); \
        double prc_test_eps_ = (double)(eps); \
        double prc_test_diff_ = prc_test_a_ - prc_test_b_; \
        if (prc_test_diff_ < 0) prc_test_diff_ = -prc_test_diff_; \
        if (prc_test_diff_ > prc_test_eps_) { \
            fprintf(stderr, "FAIL %s:%d: %s ~= %s failed (%.9g vs %.9g, eps=%.9g)\n", \
                __FILE__, __LINE__, #a, #b, prc_test_a_, prc_test_b_, prc_test_eps_); \
            exit(1); \
        } \
    } while (0)

#define PRC_ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            fprintf(stderr, "FAIL %s:%d: %s is not NULL\n", \
                __FILE__, __LINE__, #ptr); \
            exit(1); \
        } \
    } while (0)

#define PRC_ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "FAIL %s:%d: %s is NULL\n", \
                __FILE__, __LINE__, #ptr); \
            exit(1); \
        } \
    } while (0)

#endif
