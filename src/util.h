#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

void debug_print_matrix(const float* matrix, int rows, int cols);
uint64_t hamming_distance(uint64_t hash1, uint64_t hash2);
int compare_floats(const void* a, const void* b);

#endif  // UTIL_H_
