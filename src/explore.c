/**
 * explore.c
 *
 * File searching/ paths recursively to retrieve files to analyse
 **/

#include "explore.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "fourcc.h"
#include "kvec.h"
#include "log.h"
#include "util.h"

/* Wrapper to clean up a kvec containing allocated paths */
static void cleanup_alloced_paths (anu_paths *v) {
  size_t path_count = kv_size(*v);
  for (size_t i = 0; i < path_count; i++) {
    free(kv_A(*v, i));
  }
  kv_destroy(*v);
}
DEFINE_FREE(anu_paths_alloc, anu_paths, cleanup_alloced_paths(&_T))

/**
 * @brief Compare strings lexicographically.
 * Helper function for quick-sort
 * Compares 'a' with 'b' lexicographically using its ASCII values
 * e.g. a="Hello" , b="Hi"
 * (H - H) = 0
 * (e - i) --> (101 - 105) = -4 => 'b' is lexicographically before 'a'  */
static ALWAYS_INLINE int anu_cmp_str_lexicographic (const void *restrict a, const void *restrict b) {
  return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/**
 * Calls `stat()` to determine if a path is a valid directory or not.
 */
bool anu_path_is_dir (char *path) {
  struct stat statb;
  return (stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
};

void anu_file_vec_destroy (anu_file_vec *v) {

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
    if (i == ANU_VIDEO_EXT_MAX_LEN) {
      return 0;
    }

    bytes[i] = (uint8_t) anu_util_tolower(c);
  }

  /* If i was less than two characters */
  if (i < ANU_VIDEO_EXT_MIN_LEN) {
    return 0;
  }

  uint32_t ext_4cc = ANU_4CC_MAKE(bytes[0], bytes[1], bytes[2], bytes[3]);

  return anu_4cc_is_valid(ext_4cc);
}

/**
 * Resolve relative path using `realpath()`.
 *
 * @param path[in] Path to resolve.
 * @return Alloced string holding resolved path, NULL on failure.
 **/
char *anu_path_resolve (char *path) {
  errno = 0;
  char *p = realpath(path, NULL);
  if (p == NULL) {
    log_error("Could not resolve path %s : %s", path, strerror(errno));
  }
  return p;
}

/**
 * Get a file name (extension included) from path.
 *
 * @return Pointer to start of filename or `path` on failure.
 **/
char *anu_path_basename (char *path) {
  char *start = strrchr(path, '/');
  return start ? (start + 1) : path;
}

/**
 * Get filename, excluding the extension.
 **/
char *anu_path_basename_stem (char *restrict path, char *restrict out, size_t out_size) {
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
static _nonnull_(1, 2) int handle_path_pointing_to_file(char *path, anu_file_vec *files_out) {

  struct stat statb = {0};
  int stat_return = 0;
  stat_return = stat(path, &statb);
  if (stat_return) {
    log_error("Error running `stat()` on %s : (%s)", path, strerror(stat_return));
    return -1;
  }

  char *base_ptr = anu_path_basename(path);
  if (base_ptr == path) {
    log_error("(%s): Could not determine basename", path);
    return 1;
  }

  anu_file file = {.ctime = statb.st_ctime,
                   .mtime = statb.st_mtime,
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
  anu_paths dirstack;
  kv_init(dirstack);
  /* Initialise with the path received */
  kv_push(dirstack, strdup(path));
  closedir(first_dir);

  while (kv_size(dirstack) > 0) {

    /* Current path we are searching */
    char *curr_path __free(ptr) = NULL;
    curr_path = kv_pop(dirstack);

    ANU_ASSUME(curr_path != NULL);

    size_t curr_path_len = strlen(curr_path);

    /* Directory stream */
    DIR *dir __free(dir_close) = NULL;
    dir = opendir(curr_path);
    if (!dir) {
      log_warn("Could not open directory: %s", curr_path);
      continue;
    }

    /* Try getting file descriptor */
    int dir_fd = dirfd(dir);
    /* If dirfd fails then we skip */
    if (dir_fd < 0) {
      log_warn("Failed to get directory fd for: %s", curr_path);
      continue;
    }
    struct dirent *dp;

    /* Path of current file */
    log_trace("Reading directory: '%s'", curr_path);

    while ((dp = readdir(dir)) != NULL) {

      char *name = dp->d_name;

      /* Check for whether file is '.' or '..' */
      if ((name[0] == '.' && name[1] == '\0') || (name[1] == '.' && name[2] == '\0')) {
        continue;
      }

      unsigned char type = dp->d_type;
      /* Stat buffer */
      struct stat statb;
      bool stat_called = false;

      if (UNLIKELY(type == DT_UNKNOWN)) {
        if (fstatat(dir_fd, name, &statb, 0) != 0) {
          continue;
        }
        stat_called = true;
        if (S_ISDIR(statb.st_mode)) {
          type = DT_DIR;
        } else if (S_ISREG(statb.st_mode)) {
          type = DT_REG;
        } else {
          continue;  // Ignore sockets, devices, etc.
        }
      }

      /* Ignore any type of entry that is:
       * 1: Not a regular file
         2: Not a directory
         3: Not 'unknown' (symlinks, sockets, devices, etc). */
      if ((type != DT_REG) && (type != DT_DIR)) {
        continue;
      }

      /* If its a directory push it to our directory stack */
      if (type == DT_DIR) {
        char *dir_path;
        if (UNLIKELY(asprintf(&dir_path, "%s/%s", curr_path, name) == -1)) {
          log_error("Could not allocate memory for directory path!");
        }
        kv_push(dirstack, dir_path);
        continue;
      }

      /*
       * NOTE: File type is guarenteed to be 'DT_REG' (a regular file) from here
       */

      /* Check path for supported extension */
      if (!anu_path_extension_supported(name)) {
        continue;
      }

      /* Run stat on path if not already called */
      if (!stat_called && fstatat(dir_fd, dp->d_name, &statb, 0) != 0) {
        log_warn("Failed to fstatat file '%s/%s'", curr_path, name);
        continue;
      }
      if (UNLIKELY(statb.st_size == 0)) {
        log_debug("File size is 0 '%s/%s'", curr_path, name);
        continue;
      }

      /* Pointer to our final path */
      char *final_path;
      int final_path_len = asprintf(&final_path, "%s/%s", curr_path, name);

      if (UNLIKELY(final_path_len == -1)) {
        log_error("Failed to allocate memory for path variable.");
        continue;
      }

      anu_file newfile = {.size = (usize) statb.st_size,
                          .ctime = statb.st_ctime,
                          .mtime = statb.st_mtime,
                          .ino = statb.st_ino,
                          .dev = statb.st_dev,
                          .path = final_path,
                          .name_offset = (u32) (curr_path_len + 1),
                          /* TODO Change this to map to the type of file discovered
                           * But for now leave it as video as we only handle video files!
                           */
                          .media_type = ANU_MEDIA_TYPE_VIDEO};

      kv_push(*files_out, newfile);
    }
  }

  kv_destroy(dirstack);
  return 0;
}

/**
 * Scan through directories stored in config.
 *
 * @todo Add a check for hard linked files (files with same inode number)
 */
void anu_explore_scan_directories (anu_config *config, anu_paths *paths, anu_file_vec *files) {

  /* Check if we need to scan current directory */
  bool scan_curr_dir_only = ANU_HAS_ANY_FLAG(config->runtime_flags, RT_SCAN_CURR_DIR);
  if (scan_curr_dir_only) {
    char *resolved __free(ptr) = NULL;
    resolved = realpath(".", NULL);
    if (UNLIKELY(!resolved)) {
      ANU_DIE("Could not resolve current path???");
    }
    log_info("Scanning current directory: '%s'", resolved);
    if (anu_explore_recursive_filewalk(resolved, files)) {
      log_warn("Error searching for files in current directory.");
    }
    return;
  }

  /* If we're not scanning current dir, then paths_count should be NON-ZERO */
  assert(kv_size(*paths));

  /* Array to hold resolved paths */
  anu_paths real_paths __free(anu_paths_alloc);
  kv_init(real_paths);

  /* Resolve all paths before the path cleanup */
  for (size_t i = 0; i < paths->size; i++) {
    char *path = kv_A(*paths, i);
    char *resolved = realpath(kv_A(*paths, i), NULL);

    if (!resolved) {
      log_warn("Could not resolve path '%s'", path);
      continue;
    }

    /* Path successfully resolved */
    kv_push(real_paths, resolved);
  }

  size_t valid_paths = kv_size(real_paths);

  if (valid_paths == 0) {
    log_warn("No valid paths");
    return;
  }

  /* Deduplicate Paths:
   * 1) First sort paths lexicographically
   * So "/a/b" will index before "/a/b/c"
   * NOTE: The first path in the vector will always be unique.
   *
   * 2) Then we check if path A is a substring of path B
   * If A is indeed a substring of B, then B is a subdirectory of A.
   * NOTE: All directories are subdirectories of `/` (root), so we handle that
   * case specially.
   */

  /* Sort paths lexicographically:
   * So "/a/b" will be sorted before "/a/b/c" */
  qsort((void *) real_paths.items, valid_paths, sizeof(char *), anu_cmp_str_lexicographic);

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
      if ((current[prev_len] == '\0') || (current[prev_len] == '/') || (prev_len == 1 && prev[0] == '/')) {
        is_subset = true;
      }
    }

    /* If we found a redundant path */
    if (is_subset) {
      log_debug("Skipping overlapping or duplicate path: '%s' (covered by '%s')", current, prev);
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
