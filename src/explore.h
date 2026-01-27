#ifndef EXPLORE_H_
#define EXPLORE_H_

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

typedef char* u8;

typedef struct media_info {
  bool fill_later;
} media_info;

typedef struct media_flags {
  bool fill_later;
} media_flags;

typedef struct file_entry {
  u8 path;
  u8 folder;
  media_info info;
  media_flags flags;
  size_t size;

} file_entry;


int anu_open_dir (char* dir_path, DIR** out);
int anu_recursive_filewalk (char* searchp);
#endif  // EXPLORE_H_
