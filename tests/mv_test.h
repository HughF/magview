/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of magview. magview is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/* mv_test.h — the whole test framework. */
#ifndef MV_TEST_H
#define MV_TEST_H

#include <stdio.h>
#include <math.h>
#include <string.h>

static int mv_test_fails;
static int mv_test_count;

#define CHECK(cond)                                                          \
    do {                                                                     \
        mv_test_count++;                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            mv_test_fails++;                                                 \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(got, want, tol)                                           \
    do {                                                                     \
        mv_test_count++;                                                     \
        double g_ = (got), w_ = (want);                                      \
        if (!(fabs(g_ - w_) <= (tol))) {                                     \
            printf("  FAIL %s:%d  %s = %.6f, want %.6f (+/- %g)\n",          \
                   __FILE__, __LINE__, #got, g_, w_, (double)(tol));         \
            mv_test_fails++;                                                 \
        }                                                                    \
    } while (0)

#define CHECK_STR(got, want)                                                 \
    do {                                                                     \
        mv_test_count++;                                                     \
        const char *g_ = (got), *w_ = (want);                                \
        if (!g_ || strcmp(g_, w_) != 0) {                                    \
            printf("  FAIL %s:%d  %s = \"%s\", want \"%s\"\n",               \
                   __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_);        \
            mv_test_fails++;                                                 \
        }                                                                    \
    } while (0)

#define TEST_MAIN(name, ...)                                                 \
    int main(void) {                                                         \
        printf("%s\n", name);                                                \
        __VA_ARGS__                                                          \
        printf("  %d checks, %d failed\n", mv_test_count, mv_test_fails);    \
        return mv_test_fails ? 1 : 0;                                        \
    }

#endif /* MV_TEST_H */
