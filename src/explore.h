#ifndef EXPLORE_H_
#define EXPLORE_H_

#include <inttypes.h>
#include <stdbool.h>
typedef char* u8;

typedef struct media_info {
  bool fill_later;
} mediainfo;

typedef struct media_flags {
  bool fill_later;
} mediaflags;

typedef struct file_entry {
  u8 path;
  u8 folder;
  mediainfo info;
  mediaflags flags;
  long long size;

} file_entry;

#endif  // EXPLORE_H_
