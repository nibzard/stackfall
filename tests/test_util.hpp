#pragma once

// Minimal assertion helpers for the headless test suite.
// Suites call SC_CHECK; test_main.cpp prints the summary and returns
// EXIT_FAILURE when any check failed. No external framework, so the suite
// builds on every development and cross-build machine.

#include <cstdio>

namespace stackfall_test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report_check_failed(const char* file, int line,
                                const char* expression) {
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
}

inline void print_summary() {
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
}

}  // namespace stackfall_test

// Evaluates cond and records a failure, with file and line, when it is false.
#define SC_CHECK(cond)                                                 \
  do {                                                                 \
    ++stackfall_test::g_checks;                                        \
    if (!(cond)) {                                                     \
      ++stackfall_test::g_failures;                                    \
      stackfall_test::report_check_failed(__FILE__, __LINE__, #cond);  \
    }                                                                  \
  } while (false)
