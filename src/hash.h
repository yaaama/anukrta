#ifndef HASH_H_
#define HASH_H_

#include <libavutil/frame.h>
#include <stdint.h>
typedef enum anuHashType {
  ANUHASH_AVG = 0,
  ANUHASH_DCT = 1,
} anuHashType;

uint64_t average_hash(AVFrame* src_frame);
uint64_t dct_hash(AVFrame* src_frame);
#endif  // HASH_H_
