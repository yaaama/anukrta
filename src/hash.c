#include "hash.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "util.h"

/* Size of row/col len of DCT hash */
#define ANU_DCT_HASH_SIZE 8

#define ANU_PI_F 3.14159265358979323846f

/* Helper for the coefficient scaling factor in DCT-II
 *   C(u) = 1/sqrt(2) if u=0, else 1 */
static float dct_c (int u) {
  if (u == 0) {
    return 1.0F / sqrtf(2.0F);
  }
  return 1.0F;
}

uint64_t dct_hash (float* gray_2d_matrix) {

  const int rows = 32;
  const int cols = 32;

  /* Intermediate storage */
  float row_result[ANU_DCT_MATRIX_BUF_SIZE][ANU_DCT_HASH_SIZE];
  /* Final result */
  float dct_result[ANU_DCT_MATRIX_BUF_SIZE][ANU_DCT_HASH_SIZE];

  const int hash_size = ANU_DCT_HASH_SIZE;

  int N = rows;
  float scale_factor = sqrtf(2.0F / (float)N);

  float sum = 0.0F;
  /* Pass 1: 1D DCT on Rows */
  for (int y = 0; y < rows; y++) {
    for (int u = 0; u < hash_size; u++) {
      sum = 0;
      for (int x = 0; x < cols; x++) {
        /* Formula: sum += pixel[x] * cos(...) */
        sum += gray_2d_matrix[(y * cols) + x] *
               cosf(((2.0F * (float)x + 1.0F) * (float)u * ANU_PI_F) /
                    (2.0F * (float)N));
      }
      row_result[y][u] = scale_factor * dct_c(u) * sum;
    }
  }
  /* Pass 2: 1D DCT on Columns (applied to row_result) */
  for (int x = 0; x < hash_size; x++) {
    for (int v = 0; v < hash_size; v++) {
      sum = 0.0F;
      for (int y = 0; y < rows; y++) {
        sum += row_result[y][x] *
               cosf(((2.0F * (float)y + 1.0F) * (float)v * ANU_PI_F) /
                    (2.0F * (float)N));
      }
      dct_result[v][x] = scale_factor * dct_c(v) * sum;
    }
  }

  float sum_pixels = 0;
  for (int y = 0; y < hash_size; y++) {
    for (int x = 0; x < hash_size; x++) {
      /* Skip DC */
      if (x == 0 && y == 0) {
        continue;
      }
      sum_pixels += dct_result[y][x];
    }
  }

  float ac_values[ANU_DCT_HASH_SIZE * ANU_DCT_HASH_SIZE];
  int idx = 0;

  /* Collect all coefficients except [0][0] */
  for (int y = 0; y < hash_size; y++) {
    for (int x = 0; x < hash_size; x++) {
      if (x == 0 && y == 0) {
        continue;  // Skip DC
      }
      ac_values[idx++] = dct_result[y][x];
    }
  }

  /* Sort to find the median */
  qsort(ac_values, 63, sizeof(float), compare_floats);

  /*
   * The median is the middle element.
   * For 63 elements, index 31 is the exact middle.
   */
  float median = ac_values[31];

  float cmp_val = median;
  /* double cmp_val = average; */

  /* Build the 64-bit hash */
  uint64_t final_hash = 0;
  for (int y = 0; y < hash_size; y++) {
    for (int x = 0; x < hash_size; x++) {
      final_hash <<= 1;

      if (dct_result[y][x] >= cmp_val) {
        final_hash |= 1;
      }
    }
  }

  debug_print_matrix(&dct_result[0][0], hash_size, hash_size);
  return final_hash;
}
