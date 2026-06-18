#ifndef ANU_HASH_H
#define ANU_HASH_H

#include <stdint.h>

#include "util.h"

/* Size of DCT matrix */
#define ANU_PHASH_INPUT_SIZE 32
/* Number of pixels in input matrix */
#define ANU_PHASH_TOTAL_PIXELS 1024

uint64_t dct_hash(const uint8_t *restrict input_pixels)
    FUNC_NONNULL_ARG(1) FUNC_PURE;
#endif  // ANU_HASH_H
