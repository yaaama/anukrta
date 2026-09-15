#ifndef ANU_EXPLORE_H
#define ANU_EXPLORE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "kvec.h"
#include "util.h"

typedef enum ANU_MEDIA_TYPE : int32_t {
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
   * NOTE: Files can have a 0 size. */
  u64 size;

  /** Duration of video file in microseconds. */
  i64 duration_us;

  /** File mode change time. */
  i64 ctime;

  /** File modification time. */
  i64 mtime;

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
static ALWAYS_INLINE _nonnull_all_ _pure_ char *anu_file_get_filename (anu_file *f) {
  return f->path + f->name_offset;
}

/**
 * Vector type for `anu_file`.
 */
typedef kvec_t(anu_file) anu_file_vec;

/** Destructor for anu_file_vec */
void anu_file_vec_destroy(anu_file_vec *v);

DEFINE_FREE(anu_file_vec, anu_file_vec, anu_file_vec_destroy(&_T))

/**
 * Vector type of paths.
 */
typedef kvec_t(char *) anu_paths;

void anu_explore_scan_directories(anu_config *config,
                                  anu_paths *paths,
                                  anu_file_vec *files_out) _nonnull_all_;

int anu_explore_recursive_filewalk(char *path, anu_file_vec *files_out) _nonnull_all_;

int anu_path_extension_supported(char *path) _nonnull_all_ _pure_;

bool anu_path_is_dir(char *path) _nonnull_all_ _warn_unused_;

char *anu_path_resolve(char *path) _nonnull_all_ _malloc_ _warn_unused_;

char *anu_path_basename(char *path) _nonnull_all_ _warn_unused_ _pure_;

char *anu_path_basename_stem(char *restrict path, char *restrict out, size_t out_size)
    _nonnull_(1, 2) _pure_;
#endif  // ANU_EXPLORE_H
