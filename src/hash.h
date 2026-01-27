#ifndef HASH_H_
#define HASH_H_

#include <libavutil/frame.h>
#include <stdint.h>

/* Size of DCT matrix */
#define ANU_DCT_MATRIX_BUF_SIZE 32

typedef enum anuHashType {
  ANUHASH_AVG = 0,
  ANUHASH_DCT = 1,
} anuHashType;

uint64_t average_hash(float* gray_2d_matrix);
uint64_t dct_hash(float* gray_2d_matrix);
#endif  // HASH_H_
