#ifndef HASH_H_
#define HASH_H_

#include <stdint.h>

/* Size of DCT matrix */
#define ANU_PHASH_INPUT_SIZE 32

typedef enum anuHashType {
  ANU_HASH_ALGO_AVERAGE = 0,
  ANU_HASH_ALGO_DCT = 1,
} anuHashType;

uint64_t dct_hash(uint8_t* input_pixels);
#endif  // HASH_H_
