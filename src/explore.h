#ifndef ANU_EXPLORE_H
#define ANU_EXPLORE_H

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"
#include "kvec.h"
#include "util.h"

typedef struct anu_file {
  /** Path */
  char *path;

  /**
   * Size in bytes
   * NOTE: Files can have a 0 size.
   */
  size_t size;

  /** Duration of video file in microseconds */
  size_t duration_us;

  /** File mode change time */
  i64 ctime;

  /** File modification time */
  i64 mtime;

  /** Device ID (either 32bit or 64)*/
  dev_t dev;

  /** Inode number (either 32bit or 64) */
  ino_t ino;

  /** Index for when name starts in path */
  u32 name_offset;
} anu_file;

typedef struct {
  size_t size;
  size_t capacity;
  anu_file *items;
} anu_file_vec;

void anu_file_vec_destroy(anu_file_vec *v);

void scan_dirs(anukrta_config *config, anu_file_vec *files_out);
int anu_file_recursive_filewalk(char *path, anu_file_vec *files_out);
int anu_file_ext_supported(char *path);

static inline char *anu_file_get_filename (anu_file *f) {
  return f->path + f->name_offset;
}

bool anu_file_path_is_dir(char *path);
bool anu_file_path_exists(char *path);
int anu_file_resolve_relative_path(char *path, char *resolved_out);
char *anu_file_basename(char *path);
char *anu_file_basename_stem(char *path, char *out, size_t out_size);
#endif  // ANU_EXPLORE_H
