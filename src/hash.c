#include "hash.h"

#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "video.h"

#define SAVE_HASH_PGM 0
/* Size of DCT matrix */
#define ANU_DCT_MATRIX_BUF_SIZE 32
/* Size of row/col len of DCT hash */
#define ANU_DCT_HASH_SIZE 8

uint64_t average_hash (AVFrame* src_frame) {
  uint64_t hash = 0;
  int width = 8;
  int height = 8;

  /* Create small frame to hold shrunken src frame */
  AVFrame* smallframe;
  smallframe = av_frame_alloc();
  if (smallframe == NULL) {
    fprintf(stderr, "Could not allocate memory.\n");
    return 1;
  }

  if (init_gray_frame(width, height, smallframe)) {
    abort();
  }

  if (scale_frame(src_frame, width, height, smallframe)) {
    fprintf(stderr, "Failed to scale frame!");
    av_frame_free(&smallframe);
    exit(0);
  }

  uint8_t* pixels = smallframe->data[0]; /* Buffer for the 8x8 image */
  int linesize = smallframe->linesize[0];
  long sum = 0;

#if SAVE_HASH_PGM
  save_gray_frame(pixels, linesize, width, height, "-Gray", src_frame->pts);
#endif

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      sum += pixels[(y * linesize) + x];
    }
  }
  uint8_t avg = sum / 64;
  printf("Average sum of pixels: `%d`\n", avg);

#if SAVE_HASH_PGM
  /* Binary buffer for visualisation */
  uint8_t binary_viz[64];
#endif

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t val = pixels[(y * linesize) + x];

      /* If > avg, set to White (255), else Black (0) */

#if SAVE_HASH_PGM
      binary_viz[(y * 8) + x] = (val > avg) ? 255 : 0;
#endif

      /* Calculate position in the 64-bit integer */
      if (val > avg) {
        hash |= ((uint64_t)1 << (y * width + x));
      }
    }
  }

#if SAVE_HASH_PGM
  save_gray_frame(binary_viz, 8, width, height, "-binary", src_frame->pts);
#endif

  /* Cleanup */
  av_frame_free(&smallframe);
  return hash;
}

/* Helper for the coefficient scaling factor in DCT-II
 *   C(u) = 1/sqrt(2) if u=0, else 1 */
static float dct_c (int u) {
  if (u == 0) {
    return 1.0F / sqrtf(2.0F);
  }
  return 1.0F;
}

uint64_t calculate_dct_phash (const float* input_matrix_flat, int rows,
                              int cols) {

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
        sum += input_matrix_flat[(y * cols) + x] *
               cosf(((2.0F * (float)x + 1.0F) * (float)u * M_PIf) /
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
               cosf(((2.0F * (float)y + 1.0F) * (float)v * M_PIf) /
                    (2.0F * (float)N));
      }
      dct_result[v][x] = scale_factor * dct_c(v) * sum;
    }
  }

  float sum_pixels = 0;
  for (int y = 0; y < hash_size; y++) {
    for (int x = 0; x < hash_size; x++) {
      if (x == 0 && y == 0) {
        continue;  // Skip DC
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

uint64_t dct_hash (AVFrame* src_frame) {
  uint64_t final_hash = 0;
  int width = ANU_DCT_MATRIX_BUF_SIZE;
  int height = ANU_DCT_MATRIX_BUF_SIZE;

  AVFrame* gray = av_frame_alloc();
  if (gray == NULL) {
    fprintf(stderr, "Failed to initialise gray frame.\n");
    abort();
  }
  if (init_gray_frame(width, height, gray)) {
    abort();
  }

  if (scale_frame(src_frame, width, height, gray)) {
    fprintf(stderr, "Failed to scale frame!");
    /* Clean up before aborting */
    av_frame_free(&gray);
    abort();
  }

  float matrix[ANU_DCT_MATRIX_BUF_SIZE][ANU_DCT_MATRIX_BUF_SIZE] = {0};

  for (int y = 0; y < height; y++) {
    uint8_t* row_ptr = gray->data[0] + ((ptrdiff_t)y * gray->linesize[0]);
    for (int x = 0; x < width; x++) {
      matrix[y][x] = (float)row_ptr[x];
    }
  }

  final_hash = calculate_dct_phash(&matrix[0][0], 32, 32);
  av_frame_free(&gray);

  return final_hash;
}
