#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

/* One second in microseconds */
#define ANU_TIME_ONE_SEC_IN_US 1000000
/* Convert microseconds to seconds */
#define ANU_US_TO_SECONDS(A) ((A) / (ANU_TIME_ONE_SEC_IN_US))
/* Array size macro */
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
void debug_print_matrix(const float* matrix, int rows, int cols);
uint64_t hamming_distance(uint64_t hash1, uint64_t hash2);
int compare_floats(const void* a, const void* b);

int anu_util_tolower(int c);

#endif  // UTIL_H_
