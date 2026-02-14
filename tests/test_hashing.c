#include <criterion/internal/assert.h>
#include <criterion/internal/test.h>
#include <stdint.h>
#include <string.h>

#include "../src/hash.h"
#include "../src/util.h"

#define IMG_MATRIX_LEN 1024

/* Tests for when the input matrixes are the same value. */
Test (hashing, dct_flat_values) {

  /* Matrix of all 0's */
  uint8_t zero_value[IMG_MATRIX_LEN] = {0};
  uint64_t zero_hash_result = dct_hash(zero_value);

  /* Matrix of all 1's */
  uint8_t one_value[IMG_MATRIX_LEN];
  memset(one_value, 1, IMG_MATRIX_LEN);
  uint64_t one_hash_result = dct_hash(one_value);

  /* Matrix of all 100's */
  uint8_t onehundred_value[IMG_MATRIX_LEN];
  memset(onehundred_value, 100, IMG_MATRIX_LEN);
  uint64_t onehundred_hash_result = dct_hash(onehundred_value);

  cr_assert_eq(zero_hash_result, 0,
               "Matrix of all '0's should return 0. Returned: %lx instead: ",
               zero_hash_result);

  cr_assert_eq(one_hash_result, 0,
               "Matrix of all '1's should return 0. Returned: %lx instead.",
               one_hash_result);

  cr_assert_eq(onehundred_hash_result, 0,
               "Matrix of all '100's should return 0. Returned: %lx instead.",
               onehundred_hash_result);
}

Test (hashing, hamming_distances) {

  uint64_t hamming[] = {
      // 0 Distance
      0x2e4d99e644b1bf0e,
      0x2e4d99e644b1bf0e,

      // 1 Distance
      0x2e4d99e444b1bf0e,
      0x2e4d99e644b1bf0e,

      // 31
      0x2e4d99e444b1bf0e,
      0x68141b6d97eeb979,
  };
  uint64_t hamming_answers[] = {
      0,
      1,
      31,
  };
  int answer_idx = 0;
  uint64_t distance = 0;
  for (int i = 0; i < (int)ANU_ARRAY_SIZE(hamming); i += 2) {
    distance = hamming_distance(hamming[i], hamming[i + 1]);

    cr_assert_eq(distance, hamming_answers[answer_idx],
                 "Distance should be 1 for hash %lx and %lx.", hamming[i],
                 hamming[i + 1]);
    ++answer_idx;
  }
}
