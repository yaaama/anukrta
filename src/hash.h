#ifndef AK_HASH_H
#define AK_HASH_H

#include <stdint.h>

#include "util.h"

/* Size of DCT matrix */
#define AK_PHASH_INPUT_SIZE 32
/* Number of pixels in input matrix */
#define AK_PHASH_TOTAL_PIXELS 1024

AK_HOT_FUNC AK_PURE uint64_t dct_hash(const uint8_t *restrict input_pixels) AK_NONNULL_ARG(1);
#endif  // AK_HASH_H
