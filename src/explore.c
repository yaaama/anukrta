/**
 * explore.c
 *
 * File searching/ paths recursively to retrieve files to analyse
 **/
#include "explore.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "kvec.h"
#include "log.h"
#include "util.h"

typedef kvec_t(char *) path_vec;

/* Wrapper to clean up a kvec containing allocated paths */
static inline void cleanup_path_vec (path_vec *v) {
  for (size_t i = 0; i < kv_size(*v); i++) {
    free(kv_A(*v, i));
  }
  kv_destroy(*v);
}
DEFINE_FREE(path_vec, path_vec, cleanup_path_vec(&_T))

/**
 *  Declarations of static functions
 */
static int handle_path_pointing_to_file(char *p, anu_file_vec *f) _nonnull_all_;

/**
 * @brief Compare strings lexicographically.
 * Helper function for quick-sort
 * Compares 'a' with 'b' lexicographically using its ASCII values
 * e.g. a="Hello" , b="Hi"
 * (H - H) = 0
 * (e - i) --> (101 - 105) = -4 => 'b' is lexicographically before 'a'  */
static ALWAYS_INLINE int compare_strings (const void *restrict a,
                                          const void *restrict b) {
  return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* END OF DECLARATIONS */

#define fourcc_code(a, b, c, d)                                      \
  ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | \
   ((uint32_t) (d) << 24))

/*
 * X Macro Table for Video Extensions, 4CC codes and string representations
 * Format: X(Enum Identifier, char1, char2, char3, char4, lowercase, UPPERCASE)
 */
WARNING_PUSH
DISABLE_WARNING_UNUSED_ALL

#define VIDEO_EXTENSIONS_TABLE                            \
  X(ANU_EXT_4CC_3G2, '3', 'g', '2', ' ', "3g2", "3G2")    \
  X(ANU_EXT_4CC_3GP, '3', 'g', 'p', ' ', "3gp", "3GP")    \
  X(ANU_EXT_4CC_AMV, 'a', 'm', 'v', ' ', "amv", "AMV")    \
  X(ANU_EXT_4CC_ASF, 'a', 's', 'f', ' ', "asf", "ASF")    \
  X(ANU_EXT_4CC_AVI, 'a', 'v', 'i', ' ', "avi", "AVI")    \
  X(ANU_EXT_4CC_F4A, 'f', '4', 'a', ' ', "f4a", "F4A")    \
  X(ANU_EXT_4CC_F4B, 'f', '4', 'b', ' ', "f4b", "F4B")    \
  X(ANU_EXT_4CC_F4P, 'f', '4', 'p', ' ', "f4p", "F4P")    \
  X(ANU_EXT_4CC_F4V, 'f', '4', 'v', ' ', "f4v", "F4V")    \
  X(ANU_EXT_4CC_FLV, 'f', 'l', 'v', ' ', "flv", "FLV")    \
  X(ANU_EXT_4CC_GIFV, 'g', 'i', 'f', 'v', "gifv", "GIFV") \
  X(ANU_EXT_4CC_M4P, 'm', '4', 'p', ' ', "m4p", "M4P")    \
  X(ANU_EXT_4CC_M4V, 'm', '4', 'v', ' ', "m4v", "M4V")    \
  X(ANU_EXT_4CC_MKV, 'm', 'k', 'v', ' ', "mkv", "MKV")    \
  X(ANU_EXT_4CC_MNG, 'm', 'n', 'g', ' ', "mng", "MNG")    \
  X(ANU_EXT_4CC_MOD, 'm', 'o', 'd', ' ', "mod", "MOD")    \
  X(ANU_EXT_4CC_MOV, 'm', 'o', 'v', ' ', "mov", "MOV")    \
  X(ANU_EXT_4CC_MP2, 'm', 'p', '2', ' ', "mp2", "MP2")    \
  X(ANU_EXT_4CC_MP4, 'm', 'p', '4', ' ', "mp4", "MP4")    \
  X(ANU_EXT_4CC_MPE, 'm', 'p', 'e', ' ', "mpe", "MPE")    \
  X(ANU_EXT_4CC_MPEG, 'm', 'p', 'e', 'g', "mpeg", "MPEG") \
  X(ANU_EXT_4CC_MPG, 'm', 'p', 'g', ' ', "mpg", "MPG")    \
  X(ANU_EXT_4CC_MPV, 'm', 'p', 'v', ' ', "mpv", "MPV")    \
  X(ANU_EXT_4CC_MXF, 'm', 'x', 'f', ' ', "mxf", "MXF")    \
  X(ANU_EXT_4CC_NSV, 'n', 's', 'v', ' ', "nsv", "NSV")    \
  X(ANU_EXT_4CC_OGG, 'o', 'g', 'g', ' ', "ogg", "OGG")    \
  X(ANU_EXT_4CC_OGV, 'o', 'g', 'v', ' ', "ogv", "OGV")    \
  X(ANU_EXT_4CC_QT, 'q', 't', ' ', ' ', "qt", "QT")       \
  X(ANU_EXT_4CC_RM, 'r', 'm', ' ', ' ', "rm", "RM")       \
  X(ANU_EXT_4CC_ROQ, 'r', 'o', 'q', ' ', "roq", "ROQ")    \
  X(ANU_EXT_4CC_RRC, 'r', 'r', 'c', ' ', "rrc", "RRC")    \
  X(ANU_EXT_4CC_SVI, 's', 'v', 'i', ' ', "svi", "SVI")    \
  X(ANU_EXT_4CC_VOB, 'v', 'o', 'b', ' ', "vob", "VOB")    \
  X(ANU_EXT_4CC_WEBM, 'w', 'e', 'b', 'm', "webm", "WEBM") \
  X(ANU_EXT_4CC_WMV, 'w', 'm', 'v', ' ', "wmv", "WMV")    \
  X(ANU_EXT_4CC_YUV, 'y', 'u', 'v', ' ', "yuv", "YUV")

/* Enum of 4cc codes */
#define X(id, c1, c2, c3, c4, lower, upper) id = fourcc_code(c1, c2, c3, c4),

typedef enum { VIDEO_EXTENSIONS_TABLE } ANU_VIDEO_4CC;

#undef X

/* Array of 4cc codes for videos */
#define X(id, c1, c2, c3, c4, lower, upper) id,

static const uint32_t video_4ccs[] = {/* NOLINT (unused-const-variable) */
                                      VIDEO_EXTENSIONS_TABLE};
#undef X

/* Array of lowercase strings for video extensions */
#define X(id, c1, c2, c3, c4, lower, upper) lower,
static const char *video_exts_lower[] = {VIDEO_EXTENSIONS_TABLE};
#undef X

/* Array of uppercase strings for video extensions */
#define X(id, c1, c2, c3, c4, lower, upper) upper,
static const char *video_exts_upper[] = {VIDEO_EXTENSIONS_TABLE};
#undef X

static const int VIDEO_EXTENSION_MAX_LENGTH = 4;
static const int VIDEO_EXTENSION_MIN_LENGTH = 2;

WARNING_POP

/* Check extension of filename */
int anu_path_extension_supported (char *path) {
  assert(path);
  char *dot = strrchr(path, '.');

  /* Check for '.' */
  if (!dot || dot == path) {
    return 0;
  }

  /* Skip over the dot... */
  char *extension = dot + 1;

  uint8_t bytes[4] = {' ', ' ', ' ', ' '};

  int i = 0;
  for (; i < 5; i++) {
    char c = extension[i];
    /* Break if null terminator */
    if (c == '\0') {
      break;
    }
    /* Reached 5th character, extension is too long for a 4CC code */
    if (i == VIDEO_EXTENSION_MAX_LENGTH) {
      return 0;
    }

    bytes[i] = (uint8_t) anu_util_tolower(c);
  }

  /* If i was less than two characters */
  if (i < VIDEO_EXTENSION_MIN_LENGTH) {
    return 0;
  }

  uint32_t ext_4cc = fourcc_code(bytes[0], bytes[1], bytes[2], bytes[3]);

/* Match against supported formats */
/*  X outputs just the enum id followed by a comma */
#define X(id, c1, c2, c3, c4, lower, upper) id,
/* Force expansion of our X macro inside the IN_SET macro */
#define EXPAND_IN_SET(...) IN_SET(__VA_ARGS__)
  /* Call IN_SET with the macro table */
  int is_supported = EXPAND_IN_SET(
      ext_4cc,
      VIDEO_EXTENSIONS_TABLE 0x00000000 /* Dummy value to absorb the trailing comma (4cc code will never be 0) */
  );

#undef EXPAND_IN_SET
#undef X
  return is_supported;
}

/**
 * @brief Resolve a relative path.
 *
 * @param path[in] Path to resolve.
 * @return Returns path on success, anything else on failure.
 **/
char *anu_path_resolve (char *path) { return realpath(path, NULL); }

/**
 * @brief Get a file name (extension included) from path.
 * @return Success: pointer to the start of the file name.
 * Failure: Returns path pointer.
 **/
char *anu_path_basename (char *path) {
  char *start = strrchr(path, '/');
  return start ? (start + 1) : path;
}

/**
 * @brief Get filename, excluding the extension.
 **/
char *anu_path_basename_stem (char *path, char *out, size_t out_size) {
  assert(out_size > 0);

  /* Get files name */
  char *start = anu_path_basename(path);
  char *last_dot = strrchr(start, '.');

  size_t len;

  /* Logic for finding where the "stem" ends:
   * - If no dot is found, use the whole string.
   * - If the only dot is at the start (e.g. hidden file), use the whole string. */
  if (last_dot == NULL || last_dot == start) {
    len = strlen(start);
  } else {
    len = (size_t) (last_dot - start);
  }

  /* Don't overflow the 'out' buffer */
  if (len >= out_size) {
    len = out_size - 1;
  }

  memcpy(out, start, len);
  out[len] = '\0';

  return out;
}

/**
 * @brief Handle when 'path' is a file with extension we support.
 *
 * Adds the file pointed to by 'path' to the 'files_out' struct.
 **/
int handle_path_pointing_to_file (char *path, anu_file_vec *files_out) {

  struct stat statb = {0};
  int stat_return = 0;
  errno = 0;
  stat_return = stat(path, &statb);
  if (stat_return && errno) {
    perror("Error running 'stat' on file: ");
    return -1;
  }

  char *base_ptr = anu_path_basename(path);
  if (base_ptr == path) {
    log_error("(%s): Could not determine basename", path);
    return 1;
  }

  anu_file file = {.ctime = (usize) statb.st_ctime,
                   .mtime = (usize) statb.st_mtime,
                   .size = (usize) statb.st_size,
                   .path = strdup(path),
                   .name_offset = (u32) (base_ptr - path)};

  kv_push(*files_out, file);
  return 0;
}

/**
 * @brief Recursively search path and return files found.
 **/
int anu_explore_recursive_filewalk (char *path, anu_file_vec *files_out) {

  /* Test to see if we can open the directory */
  DIR *first_dir = opendir(path);

  /* If path does not open, then we can check if it is a file */
  if (!first_dir) {
    if (anu_path_extension_supported(path)) {
      log_info("Received path for regular video file: %s", path);
      return handle_path_pointing_to_file(path, files_out);
    }
    return -1;
  }

  /* Stack to hold directories */
  path_vec dirstack __free(path_vec) = {0};
  kv_init(dirstack);
  /* Initialise with the path received */
  kv_push(dirstack, strdup(path));
  closedir(first_dir);

  while (kv_size(dirstack) > 0) {

    /* Current path we are searching */
    char *curr_path __free(ptr) = NULL;
    curr_path = kv_pop(dirstack);

    ANU_ASSUME(curr_path != NULL);

    /* Directory stream */
    DIR *dir __free(dir_close) = NULL;
    dir = opendir(curr_path);
    if (!dir) {
      log_warn("Could not open directory: %s", curr_path);
      continue;
    }

    /* Try getting file descriptor */
    int dir_fd = dirfd(dir);
    /* If dirfd fails then we just try searching the current directory */
    if (dir_fd < 0) {
      dir_fd = AT_FDCWD;
    }
    struct dirent *dp;

    /* Path of current file */
    log_trace("Reading directory: '%s'", curr_path);

    while ((dp = readdir(dir)) != NULL) {

      char *name = dp->d_name;

      /* Check for whether file is '.' or '..' */
      if (name[0] == '.') {
        if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) {
          continue;
        }
      }
      unsigned char type = dp->d_type;
      /* Stat buffer */
      struct stat statb;

      /* Ignore symlinks, sockets, devices, etc. */
      if ((type != DT_REG) && (type != DT_DIR) && (type != DT_UNKNOWN)) {
        continue;
      }

      /* Check path for supported extension */
      if (type != DT_DIR && type == DT_REG &&
          !anu_path_extension_supported(name)) {
        continue;
      }

      /* If its a directory push it to our directory stack */
      if (type == DT_DIR) {
        char *dir_path;
        if (asprintf(&dir_path, "%s/%s", curr_path, name) == -1) {
          log_error("Could not allocate memory for directory path!");
        }
        kv_push(dirstack, dir_path);
        continue;
      }

      /* Run stat on path */
      if (fstatat(dir_fd, dp->d_name, &statb, 0) != 0) {
        log_warn("Failed to fstatat file '%s/%s'", curr_path, name);
        continue;
      }
      if (statb.st_size == 0) {
        log_debug("File size is 0 '%s/%s'", curr_path, name);
        continue;
      }

      /* Pointer to our final path */
      char *final_path;
      int final_path_len = asprintf(&final_path, "%s/%s", curr_path, name);

      if (final_path_len == -1) {
        log_error("Failed to allocate memory for path variable.");
        continue;
      }
      size_t name_len = strlen(name);
      anu_file newfile = {
        .size = (usize) statb.st_size,
        .ctime = (usize) statb.st_ctime,
        .mtime = (usize) statb.st_mtime,
        .ino = statb.st_ino,
        .dev = statb.st_dev,
        .path = final_path,
        .name_offset = (u32) ((size_t) final_path_len - name_len),
        .media_type = ANU_MEDIA_TYPE_VIDEO};

      kv_push(*files_out, newfile);
    }
  }

  kv_destroy(dirstack);
  return 0;
}

/* TODO: Add a check for hard linked files (files with same inode number) */
void anu_explore_scan_directories (anukrta_config *config,
                                   anu_file_vec *files) {

  /* Check if we need to scan current directory */
  if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_SCAN_CURR_DIR)) {

    char *resolved __free(ptr) = NULL;
    resolved = realpath(".", NULL);
    if (!resolved) {
      ANU_DIE("Could not resolve current path.");
    }

    log_info("Scanning current directory (%s)", resolved);
    if (anu_explore_recursive_filewalk(resolved, files)) {
      log_warn("Error searching for files in current directory.");
    }
    return;
  }

  /* If we're not scanning current dir, then paths_count should be non zero */
  assert(config->paths_count > 0);

  /* Array to hold resolved absolute paths */
  path_vec real_paths __free(path_vec) = {0};
  kv_init(real_paths);

  /* Resolve all paths before the path cleanup */
  for (size_t i = 0; i < config->paths_count; i++) {
    char *resolved = realpath(config->paths[i], NULL);
    if (resolved != NULL) {
      kv_push(real_paths, resolved);
    } else {
      log_warn("Could not resolve path '%s'", config->paths[i]);
    }
  }

  size_t valid_paths = kv_size(real_paths);

  if (valid_paths == 0) {
    log_warn("No valid paths");
    return;
  }

  /* Deduplicate Paths */

  /* Sort paths lexicographically:
   * So "/a/b" will be sorted before "/a/b/c" */
  qsort((void *) real_paths.items, valid_paths, sizeof(char *),
        compare_strings);

  size_t unique_path_idx = 1;

  for (size_t i = 1; i < valid_paths; i++) {
    char *prev = kv_A(real_paths, (unique_path_idx - 1));
    char *current = kv_A(real_paths, i);
    size_t prev_len = strlen(prev);

    bool is_subset = false;

    /* Check if 'current' starts with 'prev' */
    if (strncmp(prev, current, prev_len) == 0) {

      /* Ensure it's an exact match or an actual subdirectory,
       * avoiding similar names (e.g. prev="/dir", curr="/dir-2")
       * Also handle cases where path is '/'
       */
      if ((current[prev_len] == '\0') || (current[prev_len] == '/') ||
          (prev_len == 1 && prev[0] == '/')) {
        is_subset = true;
      }
    }

    /* If we found a redundant path */
    if (is_subset) {
      log_debug(
          "Skipping overlapping or duplicate path: '%s' (covered by '%s')",
          current, prev);
      /* Free the redundant path as we are now removing it from the vector */
      free(current);

    } else {
      kv_A(real_paths, unique_path_idx) = current; /* Keep the unique path */
      ++unique_path_idx;
    }
  }

  kv_size(real_paths) = unique_path_idx;

  /* Now we filewalk only unique paths */
  for (size_t i = 0; i < unique_path_idx; i++) {
    char *path = kv_A(real_paths, i);
    if (anu_explore_recursive_filewalk(path, files)) {
      log_warn("Error searching for files in '%s'", path);
    }
  }
}
