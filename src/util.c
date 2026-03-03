#include "util.h"

#include <stdint.h>
#include <stdio.h>

/* Helper to visualise matrix */
void debug_print_matrix (const float *matrix, int rows, int cols) {
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

i32 hamming_distance (uint64_t hash1, uint64_t hash2) {

  return __builtin_popcountll(hash1 ^ hash2);
}

void anu_util_print_indent (int depth) {
  for (int i = 0; i < depth; i++) {
    /* 4 spaces per level */
    printf("    ");
  }
}
