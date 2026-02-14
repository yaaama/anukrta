#ifndef ANU_HASH_H
#define ANU_HASH_H

#include <stdint.h>

/* Size of DCT matrix */
#define ANU_PHASH_INPUT_SIZE 32

typedef enum anu_hash_type {
  ANU_HASH_ALGO_AVERAGE = 0,
  ANU_HASH_ALGO_DCT = 1,
} anu_hash_type;

uint64_t dct_hash(uint8_t *input_pixels);
#endif  // ANU_HASH_H
