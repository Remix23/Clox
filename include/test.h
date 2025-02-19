#ifndef clox_tests_h
#define clox_tests_h

typedef enum {
    TEST_OK,
    TEST_FAILURE
} TestResult;

void testerRunner (const char* test_path);

#endif