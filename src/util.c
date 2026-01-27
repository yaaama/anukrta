#include "util.h"

#include <stdint.h>
#include <stdio.h>

/* Helper to visualise matrix */
void debug_print_matrix (const float* matrix, int rows, int cols) {
  printf("--- %dx%d Visual Dump ---\n", cols, rows);
  for (int y = 0; y < rows; y += 2) {  // Skip every other row to fit screen
    for (int x = 0; x < cols; x++) {
      float val = matrix[(y * cols) + x];
      /* Simple ASCII mapping */
      char c = ' ';
      if (val > 200) {
        c = '#';
      } else if (150 < val) {
        c = '+';
      } else if (100 < val) {
        c = ':';
      } else if (50 < val) {
        c = '.';
      }
      printf("%c", c);
    }
    printf("\n");
  }
  printf("-------------------------\n");
}

uint64_t hamming_distance (uint64_t hash1, uint64_t hash2) {
  uint64_t x =
      hash1 ^
      hash2; /* XOR finds the differences (returns 1 where bits differ) */
  uint64_t dist = 0;

  /* Count the number of 1s (Kernighan's algorithm) */
  while (x) {
    dist++;
    x &= x - 1;
  }
  return dist;
}

int compare_floats (const void* a, const void* b) {
  double arg1 = *(const float*)a;
  double arg2 = *(const float*)b;

  if (arg1 < arg2) {
    return -1;
  }
  if (arg1 > arg2) {
    return 1;
  }
  return 0;
}
