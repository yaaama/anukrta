#include "hash.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Size of row/col len of DCT hash */
#define ANU_PHASH_DCT_SIZE 8

/** Intermediate buffer length for DCT calculation
 * (ANU_PHASH_INPUT_SIZE * ANU_PHASH_DCT_SIZE)
 * (32 * 8)
 */
#define DCT_INTERMEDIATE_BUF_LEN (ANU_PHASH_INPUT_SIZE * ANU_PHASH_DCT_SIZE)

/**
 * Final DCT digest length in bits
 * ANU_PHASH_DCT_SIZE^2
 */
#define DCT_DIGEST_LEN (ANU_PHASH_DCT_SIZE * ANU_PHASH_DCT_SIZE)

/** Scale factor: 2^15 (32768) */
#define DCT_INT_FIXED_SHIFT 15
/* When we multiply two 15 bit numbers, we get 30 bits */
#define DCT_INT_SCALE_BITS_Q30 (DCT_INT_FIXED_SHIFT * 2)
#define DCT_INT_FIXED_SCALE_Q30 (1LL << DCT_INT_SCALE_BITS_Q30)

/** Error threshold for when working with FLOAT weights */
#define DCT_FLOAT_EPISILON 0.001F

/** Error threshold when working with scaled fixed-point weights */

/* PI in floating point format */
#define ANU_PI_F 3.14159265358979323846F
#define DCT_INT_EPISILON ((int64_t) (DCT_INT_FIXED_SCALE_Q30 / 1000))

/**
 * Number of coefficients storing image detail
 * (1 of the values is the brightness aka the DC coefficient)
 */
#define DCT_AC_COEFFICIENT_COUNT (DCT_DIGEST_LEN - 1)

#if ANU_PHASH_DCT_SIZE != 8
#  warning "dct weights will not work if dct size is not 8."
#endif

/* Pre-calculated 1D integer DCT weights for 32x32 to 8x8 pHash
 *   Format: Q15 fixed-point (shifted left by 15 bits) */
static const int32_t DCT_WEIGHTS_INT[256] = {
  /* clang-format off */
  /* u = 0 */
  5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793,
  5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793, 5793,
  5793, 5793, 5793, 5793, 5793, 5793,
  /* u = 1 */
    8182, 8103, 7946, 7713, 7405, 7027, 6580, 6070, 5501, 4880, 4212, 3503, 2760,
  1990, 1202, 402, -402, -1202, -1990, -2760, -3503, -4212, -4880, -5501, -6070,
  -6580, -7027, -7405, -7713, -7946, -8103, -8182,
  /* u = 2 */
    8153, 7839, 7225, 6333, 5197, 3862, 2378, 803, -803, -2378, -3862, -5197,
  -6333, -7225, -7839, -8153, -8153, -7839, -7225, -6333, -5197, -3862, -2378,
  -803, 803, 2378, 3862, 5197, 6333, 7225, 7839, 8153,
  /* u = 3 */
    8103, 7405, 6070, 4212, 1990, -402, -2760, -4880, -6580, -7713, -8182, -7946,
  -7027, -5501, -3503, -1202, 1202, 3503, 5501, 7027, 7946, 8182, 7713, 6580,
  4880, 2760, 402, -1990, -4212, -6070, -7405, -8103,
  /* u = 4 */
    8035, 6811, 4551, 1598, -1598, -4551, -6811, -8035, -8035, -6811, -4551,
  -1598, 1598, 4551, 6811, 8035, 8035, 6811, 4551, 1598, -1598, -4551, -6811,
  -8035, -8035, -6811, -4551, -1598, 1598, 4551, 6811, 8035,
  /* u = 5 */
    7946, 6070, 2760, -1202, -4880, -7405, -8182, -7027, -4212, -402, 3503, 6580,
  8103, 7713, 5501, 1990, -1990, -5501, -7713, -8103, -6580, -3503, 402, 4212,
  7027, 8182, 7405, 4880, 1202, -2760, -6070, -7946,
  /* u = 6 */
    7839, 5197, 803, -3862, -7225, -8153, -6333, -2378, 2378, 6333, 8153, 7225,
  3862, -803, -5197, -7839, -7839, -5197, -803, 3862, 7225, 8153, 6333, 2378,
  -2378, -6333, -8153, -7225, -3862, 803, 5197, 7839,
  /* u = 7 */
    7713, 4212, -1202, -6070, -8182, -6580, -1990, 3503, 7405, 7946, 4880, -402,
  -5501, -8103, -7027, -2760, 2760, 7027, 8103, 5501, 402, -4880, -7946, -7405,
  -3503, 1990, 6580, 8182, 6070, 1202, -4212, -7713,
  /* clang-format on */
};

uint64_t dct_hash (const uint8_t *restrict input_pixels) {

  int32_t row_result[DCT_INTERMEDIATE_BUF_LEN];
  int64_t dct_result[DCT_DIGEST_LEN];

  /* Pass 1: 1D DCT on Rows */

  for (ptrdiff_t u = 0; u < ANU_PHASH_DCT_SIZE; u++) {
    const int32_t *restrict weight_ptr =
        &DCT_WEIGHTS_INT[u * ANU_PHASH_INPUT_SIZE];

    for (ptrdiff_t y = 0; y < ANU_PHASH_INPUT_SIZE; y++) {
      const uint8_t *restrict row_ptr =
          &input_pixels[(y * ANU_PHASH_INPUT_SIZE)];

      int32_t sum = 0;

      /* Vectorizable by compiler */
      for (ptrdiff_t x = 0; x < ANU_PHASH_INPUT_SIZE; x++) {
        sum += (int32_t) row_ptr[x] * weight_ptr[x];
      }

      /* Max value is ~66 million, safely fits inside int32_t */
      row_result[(u * ANU_PHASH_INPUT_SIZE) + y] = sum;
    }
  }

  /* Pass 2: 1D DCT on Columns */
  for (ptrdiff_t u = 0; u < ANU_PHASH_DCT_SIZE; u++) {
    /* u is our output row index */
    const int32_t *restrict row_of_t = &row_result[u * ANU_PHASH_INPUT_SIZE];

    for (ptrdiff_t v = 0; v < ANU_PHASH_DCT_SIZE; v++) {
      /* v is our output column index */
      const int32_t *restrict weight_ptr =
          &DCT_WEIGHTS_INT[v * ANU_PHASH_INPUT_SIZE];

      int64_t sum = 0;

      for (ptrdiff_t y = 0; y < ANU_PHASH_INPUT_SIZE; y++) {
        /* Max sum is ~1.75 * 10^13, safely fits inside int64_t */
        sum += (int64_t) row_of_t[y] * weight_ptr[y];
      }
      dct_result[(u * ANU_PHASH_DCT_SIZE) + v] = sum;
    }
  }

  /* Sum up the pixels to calculate the average (excluding DC coefficient at index 0) */
  int64_t sum_pixels = 0;
  for (int i = 1; i < DCT_DIGEST_LEN; i++) {
    sum_pixels += dct_result[i];
  }

  /* Calculate threshold.
   * With 15-bit weights, total scale is 2^30.
   * Float epsilon 0.001 * 2^30 = 1073741. */
  int64_t threshold =
      (sum_pixels / DCT_AC_COEFFICIENT_COUNT) + DCT_INT_EPISILON;

  /* Build the 64-bit hash */
  uint64_t final_hash = 0;
  for (int i = 1; i < DCT_DIGEST_LEN; i++) {
    final_hash = (final_hash << 1) | (dct_result[i] > threshold);
  }

  return final_hash;
}
