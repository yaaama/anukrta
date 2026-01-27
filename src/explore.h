#ifndef EXPLORE_H_
#define EXPLORE_H_

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "stack.h"

typedef char* u8;

#define ANU_MAX_PATH_LEN 512

typedef struct media_info {
  bool fill_later;
} media_info;

typedef struct media_flags {
  bool fill_later;
} media_flags;

typedef struct anuFile {
  /* Path */
  char path[ANU_MAX_PATH_LEN];
  /* Size in bytes */
  size_t size;
  long ctime;
  char name[256];
} anuFile;

int anu_open_dir(char* dir_path, DIR** out);
int anu_recursive_filewalk(char* searchp, anuStack* files_out);
#endif  // EXPLORE_H_
