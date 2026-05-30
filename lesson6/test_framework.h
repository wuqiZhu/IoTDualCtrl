/**
 * @file test_framework.h
 * @brief 简单单元测试框架
 * @author zhuxiangbo
 * @date 2026-05-23
 * @version 1.0
 *
 * 提供简单的单元测试功能，用于验证不依赖硬件的函数。
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              测试宏定义 */
/* ========================================================================== */

/** @brief 测试计数器 */
static int test_pass_count = 0;
static int test_fail_count = 0;
static int test_total_count = 0;

/** @brief 断言：相等（通用版本，支持int和size_t） */
#define ASSERT_EQUAL(expected, actual)                                         \
  do {                                                                         \
    test_total_count++;                                                        \
    if ((long long)(expected) == (long long)(actual)) {                        \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Expected %lld, got %lld\n", __FILE__, __LINE__,   \
             (long long)(expected), (long long)(actual));                      \
    }                                                                          \
  } while (0)

/** @brief 断言：不相等（通用版本，支持int和size_t） */
#define ASSERT_NOT_EQUAL(expected, actual)                                     \
  do {                                                                         \
    test_total_count++;                                                        \
    if ((long long)(expected) != (long long)(actual)) {                        \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Expected not %lld, got %lld\n", __FILE__,         \
             __LINE__, (long long)(expected), (long long)(actual));            \
    }                                                                          \
  } while (0)

/** @brief 断言：为真 */
#define ASSERT_TRUE(condition)                                                 \
  do {                                                                         \
    test_total_count++;                                                        \
    if ((condition)) {                                                         \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Condition is false\n", __FILE__, __LINE__);       \
    }                                                                          \
  } while (0)

/** @brief 断言：为假 */
#define ASSERT_FALSE(condition)                                                \
  do {                                                                         \
    test_total_count++;                                                        \
    if (!(condition)) {                                                        \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Condition is true\n", __FILE__, __LINE__);        \
    }                                                                          \
  } while (0)

/** @brief 断言：字符串相等 */
#define ASSERT_STRING_EQUAL(expected, actual)                                  \
  do {                                                                         \
    test_total_count++;                                                        \
    if (strcmp((expected), (actual)) == 0) {                                   \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Expected \"%s\", got \"%s\"\n", __FILE__,         \
             __LINE__, (expected), (actual));                                  \
    }                                                                          \
  } while (0)

/** @brief 断言：指针不为空 */
#define ASSERT_NOT_NULL(ptr)                                                   \
  do {                                                                         \
    test_total_count++;                                                        \
    if ((ptr) != NULL) {                                                       \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Pointer is NULL\n", __FILE__, __LINE__);          \
    }                                                                          \
  } while (0)

/** @brief 断言：指针为空 */
#define ASSERT_NULL(ptr)                                                       \
  do {                                                                         \
    test_total_count++;                                                        \
    if ((ptr) == NULL) {                                                       \
      test_pass_count++;                                                       \
    } else {                                                                   \
      test_fail_count++;                                                       \
      printf("  FAIL: %s:%d: Pointer is not NULL\n", __FILE__, __LINE__);      \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/*                              测试函数宏 */
/* ========================================================================== */

/** @brief 开始测试套件 */
#define TEST_SUITE_BEGIN(name)                                                 \
  void test_suite_##name(void) {                                               \
    printf("\n=== Test Suite: %s ===\n", #name);

/** @brief 结束测试套件 */
#define TEST_SUITE_END()                                                       \
  printf("\n=== Test Results ===\n");                                          \
  printf("Total: %d, Pass: %d, Fail: %d\n", test_total_count, test_pass_count, \
         test_fail_count);                                                     \
  if (test_fail_count == 0) {                                                  \
    printf("All tests passed!\n");                                             \
  } else {                                                                     \
    printf("Some tests failed!\n");                                            \
  }                                                                            \
  }

/** @brief 运行测试函数 */
#define RUN_TEST(test_func)                                                    \
  do {                                                                         \
    printf("Running %s...\n", #test_func);                                     \
    test_func();                                                               \
  } while (0)

/* ========================================================================== */
/*                              测试函数声明 */
/* ========================================================================== */

/**
 * @brief 运行所有测试
 */
void run_all_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_FRAMEWORK_H */
