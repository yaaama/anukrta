#ifndef ANU_HASH_H
#define ANU_HASH_H

#include <stdint.h>

#include "util.h"

/* Size of DCT matrix */
#define ANU_PHASH_INPUT_SIZE 32
/* Number of pixels in input matrix */
#define ANU_PHASH_TOTAL_PIXELS 1024

HOT_FUNC _pure_ uint64_t dct_hash(const uint8_t *restrict input_pixels) _nonnull_(1);
#endif  // ANU_HASH_H
