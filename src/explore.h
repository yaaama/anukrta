#ifndef ANU_EXPLORE_H
#define ANU_EXPLORE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "kvec.h"
#include "util.h"

typedef enum ANU_MEDIA_TYPE {
  ANU_MEDIA_TYPE_UNKNOWN = -1,
  ANU_MEDIA_TYPE_VIDEO,
  ANU_MEDIA_TYPE_IMAGE,
  ANU_MEDIA_TYPE_AUDIO,
} ANU_MEDIA_TYPE;

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

  /** Device ID */
  u64 dev;

  /** Inode number */
  u64 ino;

  /** Index for when name starts in path. */
  u32 name_offset;

  /** Media type. */
  enum ANU_MEDIA_TYPE media_type;
} anu_file;

/**
 * Helper function to retrieve filename stored in `anu_file`.
 */
static ALWAYS_INLINE _nonnull_all_ char *anu_file_get_filename (anu_file *f) {
  return f->path + f->name_offset;
}

/**
 * @brief Vector type for anu_file.
 */
typedef kvec_t(anu_file) anu_file_vec;

/** Destructor for anu_file_vec */
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

void anu_explore_scan_directories(anu_config *config, anu_file_vec *files_out) _nonnull_all_;

int anu_explore_recursive_filewalk(char *path, anu_file_vec *files_out) _nonnull_all_;

int anu_path_extension_supported(char *path) _nonnull_all_ _pure_;

static ALWAYS_INLINE _nonnull_all_ bool anu_path_is_dir (char *path) {
  struct stat statb;
  return (stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
};

char *anu_path_resolve(char *path) _nonnull_all_ _malloc_ _warn_unused_;

char *anu_path_basename(char *path) _nonnull_all_ _warn_unused_ _pure_;

char *anu_path_basename_stem(char *restrict path, char *restrict out, size_t out_size)
    _nonnull_(1, 2) _pure_;
#endif  // ANU_EXPLORE_H
