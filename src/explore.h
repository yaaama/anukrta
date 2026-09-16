#ifndef AK_EXPLORE_H
#define AK_EXPLORE_H

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

typedef enum AK_MEDIA_TYPE : int32_t {
  AK_MEDIA_TYPE_UNKNOWN = -1,
  AK_MEDIA_TYPE_VIDEO,
  AK_MEDIA_TYPE_IMAGE,
  AK_MEDIA_TYPE_AUDIO,
} AK_MEDIA_TYPE;

typedef struct ak_file {
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
  enum AK_MEDIA_TYPE media_type;
} ak_file;

/**
 * Helper function to retrieve filename stored in `ak_file`.
 */
static AK_ALWAYS_INLINE AK_NONNULL_ALL AK_PURE char *ak_file_name (ak_file *f) {
  return f->path + f->name_offset;
}

/**
 * Vector type for `ak_file`.
 */
typedef kvec_t(ak_file) ak_file_v;

/** Destructor for ak_file_vec */
void ak_file_v_destroy(ak_file_v *v);

AK_DEFINE_AUTO(file_v, ak_file_v, ak_file_v_destroy(&_T))

/**
 * Vector type of paths.
 */
typedef kvec_t(char *) ak_paths;

void ak_explore_scan_paths(ak_config *config, ak_paths *paths, ak_file_v *files_out) AK_NONNULL_ALL;

int ak_explore_filewalk(char *path, ak_file_v *files_out) AK_NONNULL_ALL;

int ak_explore_ext_supported(char *path) AK_NONNULL_ALL AK_PURE;

bool ak_explore_is_dir(char *path) AK_NONNULL_ALL AK_NO_DISCARD;

char *ak_path_resolve(char *path) AK_NONNULL_ALL AK_MALLOC AK_NO_DISCARD;

char *ak_path_basename(char *path) AK_NONNULL_ALL AK_NO_DISCARD AK_PURE;

char *ak_path_basename_stem(char *restrict path, char *restrict out, size_t out_size) AK_NONNULL_ARG(1, 2)
AK_PURE;
#endif  // AK_EXPLORE_H
