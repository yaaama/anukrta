#include "hash.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "util.h"

/* Size of row/col len of DCT hash */
#define ANU_DCT_HASH_SIZE 8
#define ANU_DCT_HORIZONTAL_LEN 256
#define ANU_DCT_FINAL_LEN 64

#define ANU_PI_F 3.14159265358979323846f

static float dct_weights[ANU_DCT_HASH_SIZE][ANU_DCT_MATRIX_BUF_SIZE];
static bool dct_lut_initialised = false;

/* Initialize the LUT once */
static void init_dct_tables (void) {
  if (dct_lut_initialised) {
    return;
  }

  float scale_common = sqrtf(2.0F / ANU_DCT_MATRIX_BUF_SIZE);

  for (int u = 0; u < ANU_DCT_HASH_SIZE; u++) {
    /* C(u) scaling */
    float cu = (u == 0) ? (1.0F / sqrtf(2.0F)) : 1.0F;

    for (int x = 0; x < ANU_DCT_MATRIX_BUF_SIZE; x++) {
      float angle =
          ((2.0F * (float)x + 1.0F) * (float)u * ANU_PI_F) / (2.0F * ANU_DCT_MATRIX_BUF_SIZE);
      dct_weights[u][x] = cu * scale_common * cosf(angle);
    }
  }
  dct_lut_initialised = true;
}

/* Helper for the coefficient scaling factor in DCT-II
 *   C(u) = 1/sqrt(2) if u=0, else 1 */
static float dct_c (int u) {
  if (u == 0) {
    return 1.0F / sqrtf(2.0F);
  }
  return 1.0F;
}

uint64_t dct_hash (float* gray_2d_matrix) {

  if (!dct_lut_initialised) {
    init_dct_tables();
  }

  const int rows = 32;
  const int cols = 32;

  /* Intermediate storage */
  float row_result[ANU_DCT_HORIZONTAL_LEN];
  /* Final result */
  float dct_result[ANU_DCT_FINAL_LEN];

  const int hash_size = ANU_DCT_HASH_SIZE;

  float sum = 0.0F;
  float* row_ptr;
  /* Pass 1: 1D DCT on Rows */
  for (int y = 0; y < ANU_DCT_MATRIX_BUF_SIZE; y++) {
    row_ptr = &gray_2d_matrix[((ptrdiff_t)y * ANU_DCT_MATRIX_BUF_SIZE)];

    for (int u = 0; u < ANU_DCT_HASH_SIZE; u++) {
      sum = 0;

      for (int x = 0; x < ANU_DCT_MATRIX_BUF_SIZE; x++) {
        /* Formula: sum += pixel[x] * cos(...) */
        sum += row_ptr[x] * (dct_weights[u][x]);
      }
      row_result[(y * ANU_DCT_HASH_SIZE) + u] = sum;
    }
  }
  /* Pass 2: 1D DCT on Columns (applied to row_result) */
  for (int x = 0; x < ANU_DCT_HASH_SIZE; x++) {
    for (int v = 0; v < ANU_DCT_HASH_SIZE; v++) {
      sum = 0.0F;
      for (int y = 0; y < ANU_DCT_MATRIX_BUF_SIZE; y++) {
        sum += row_result[(ptrdiff_t)((y * ANU_DCT_HASH_SIZE) + x)] * (dct_weights[v][y]);
      }
      dct_result[(v * ANU_DCT_HASH_SIZE) + x] = sum;
    }
  }

  float sum_pixels = 0;
  for (int i = 1; i < ANU_DCT_FINAL_LEN; i++) {
    sum_pixels += dct_result[i];
  }

  float average = sum_pixels / (ANU_DCT_FINAL_LEN - 1);

  /* Build the 64-bit hash */
  uint64_t final_hash = 0;
  for (int i = 0; i < ANU_DCT_FINAL_LEN; i++) {
    final_hash <<= 1;

    if (dct_result[i] > average) {
      final_hash |= 1;
    }
  }

  debug_print_matrix(&dct_result[0], hash_size, hash_size);
  return final_hash;
}
