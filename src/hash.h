#ifndef ANU_HASH_H
#define ANU_HASH_H

#include <stdint.h>

/* Size of DCT matrix */
#define ANU_PHASH_INPUT_SIZE 32
/* Number of pixels in input matrix */
#define ANU_PHASH_TOTAL_PIXELS 1024

uint64_t dct_hash(uint8_t input_pixels[static ANU_PHASH_TOTAL_PIXELS]);
#endif  // ANU_HASH_H
