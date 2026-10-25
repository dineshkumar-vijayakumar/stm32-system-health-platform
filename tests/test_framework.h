/**
 * @file    test_framework.h
 * @brief   Minimal, dependency-free unit test framework for embedded C modules.
 *
 * @details Real embedded/automotive codebases (including Bosch's Eclipse
 *          S-CORE) use Google Test on the host to unit-test target code
 *          before it ever touches hardware. GTest itself is too heavy to
 *          pull in for a small portfolio project, so this file provides
 *          the same *shape* of API (TEST macro, ASSERT/EXPECT macros,
 *          pass/fail reporting) using nothing but the C standard library.
 *
 *          This lets diag_log / diag / diag_watchdog be tested on a PC
 *          (fast iteration) using the exact same source files that get
 *          cross-compiled for the STM32 target -- the same principle
 *          Bosch's own test docs describe: "requirements-based test",
 *          run on host, before deployment to target.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static const char* g_current_test_name = NULL;

#define TEST(suite, name) \
    static void suite##_##name(void); \
    static void run_##suite##_##name(void) \
    { \
        g_current_test_name = #suite "." #name; \
        g_tests_run++; \
        suite##_##name(); \
    } \
    static void suite##_##name(void)

#define RUN_TEST(suite, name) run_##suite##_##name()

#define EXPECT_EQ(actual, expected) \
    do { \
        if ((actual) != (expected)) { \
            printf("  [FAIL] %s: EXPECT_EQ failed at %s:%d\n" \
                   "         expected=%ld actual=%ld\n", \
                   g_current_test_name, __FILE__, __LINE__, \
                   (long)(expected), (long)(actual)); \
            g_tests_failed++; \
        } \
    } while (0)

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s: EXPECT_TRUE failed at %s:%d (%s)\n", \
                   g_current_test_name, __FILE__, __LINE__, #cond); \
            g_tests_failed++; \
        } \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_STREQ(actual, expected) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            printf("  [FAIL] %s: EXPECT_STREQ failed at %s:%d\n" \
                   "         expected=\"%s\" actual=\"%s\"\n", \
                   g_current_test_name, __FILE__, __LINE__, \
                   (expected), (actual)); \
            g_tests_failed++; \
        } \
    } while (0)

#define TEST_SUMMARY() \
    do { \
        printf("\n=== %d tests run, %d failed ===\n", g_tests_run, g_tests_failed); \
        if (g_tests_failed > 0) { return 1; } \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
