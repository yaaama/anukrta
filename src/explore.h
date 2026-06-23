#ifndef ANU_EXPLORE_H
#define ANU_EXPLORE_H

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"
#include "kvec.h"
#include "util.h"

typedef struct anu_file {
  /** Path of file. */
  char *path;

  /**
   * Size in bytes.
   * NOTE: Files can have a 0 size.
   */
  size_t size;

  /** Duration of video file in microseconds. */
  size_t duration_us;

  /** File mode change time. */
  u64 ctime;

  /** File modification time. */
  u64 mtime;

  /** Device ID (either 32bit or 64). */
  dev_t dev;

  /** Inode number (either 32bit or 64). */
  ino_t ino;

  /** Index for when name starts in path. */
  u32 name_offset;
} anu_file;

/**
 * Helper function to retrieve filename stored in `anu_file`.
 */
static ALWAYS_INLINE FUNC_NONNULL_ALL char *anu_file_get_filename (
    anu_file *f) {
  return f->path + f->name_offset;
}

/**
 * @brief Vector type for anu_file.
 */
typedef kvec_t(anu_file) anu_file_vec;

/* Destructor for anu_file_vec */
static ALWAYS_INLINE void anu_file_vec_destroy (anu_file_vec *v) {

  if (!v) {
    return;
  }

  size_t sz = kv_size(*v);
  anu_file *file = NULL;

  for (size_t i = 0; i < sz; i++) {
    file = &kv_A(*v, i);
    /* Must free the dynamically allocated path strings */
    free(file->path);
  }
  kv_destroy(*v);
}

/* Define auto cleanup function */
DEFINE_FREE(anu_file_vec, anu_file_vec, anu_file_vec_destroy(&_T))

void anu_explore_scan_directories(anukrta_config *config,
                                  anu_file_vec *files_out) FUNC_NONNULL_ALL;

int anu_explore_recursive_filewalk(char *path,
                                   anu_file_vec *files_out) FUNC_NONNULL_ALL;

int anu_path_extension_supported(char *path) FUNC_NONNULL_ALL;

static ALWAYS_INLINE FUNC_NONNULL_ALL bool anu_path_is_dir (char *path) {
  struct stat statb;
  return (stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
};

ALWAYS_INLINE bool anu_path_exists(char *path) FUNC_NONNULL_ALL MUST_CHECK;

char *anu_path_resolve(char *path) FUNC_NONNULL_ALL FUNC_MALLOC MUST_CHECK;

char *anu_path_basename(char *path) FUNC_NONNULL_ALL MUST_CHECK;

char *anu_path_basename_stem(char *restrict path,
                             char *restrict out,
                             size_t out_size) FUNC_NONNULL_ARG(1, 2);
#endif  // ANU_EXPLORE_H
