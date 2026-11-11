/*
 * A very small unit test helper. No external library is necessary.
 *
 *   TEST(my_test) { CHECK_EQ(1 + 1, 2); }
 *   ...
 *   int main(void) { RUN(my_test); return utest_finish(); }
 */
#ifndef _UTEST_H
#define _UTEST_H

#include <stdio.h>

static int utest_checks = 0;
static int utest_failures = 0;
static const char* utest_current = "";

#define TEST(_name) static void test_##_name(void)

#define RUN(_name) do {             \
    utest_current = #_name;         \
    test_##_name();                 \
  } while(0)

#define CHECK(_cond) do {                                                               \
    utest_checks++;                                                                     \
    if(!(_cond)) {                                                                      \
      utest_failures++;                                                                 \
      printf("FAIL %s (%s:%d): %s\n", utest_current, __FILE__, __LINE__, #_cond);       \
    }                                                                                   \
  } while(0)

#define CHECK_EQ(_actual, _expected) do {                                               \
    long long utestActual = (long long)(_actual);                                       \
    long long utestExpected = (long long)(_expected);                                   \
    utest_checks++;                                                                     \
    if(utestActual != utestExpected) {                                                  \
      utest_failures++;                                                                 \
      printf("FAIL %s (%s:%d): %s is %lld, expected %lld\n", utest_current, __FILE__,   \
             __LINE__, #_actual, utestActual, utestExpected);                           \
    }                                                                                   \
  } while(0)

static inline int utest_finish(void) {
  printf("%d checks, %d failures\n", utest_checks, utest_failures);
  return (utest_failures == 0) ? 0 : 1;
}

#endif // _UTEST_H
