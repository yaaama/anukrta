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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "kvec.h"
#include "log.h"
#include "util.h"

/* Video extensions */
static const char *video_extensions[] = {
  "3g2", "3gp",  "amv",  "asf", "avi", "f4a", "f4b",  "f4p", "f4v",
  "flv", "gifv", "m4p",  "m4v", "mkv", "mng", "mod",  "mov", "mp2",
  "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf", "nsv",  "ogg", "ogv",
  "qt",  "rm",   "roq",  "rrc", "svi", "vob", "webm", "wmv", "yuv"};

static const size_t VIDEO_EXTENSIONS_COUNT = ANU_ARRAY_SIZE(video_extensions);

static const int VIDEO_EXTENSION_MAX_LENGTH = 4;
static const int VIDEO_EXTENSION_MIN_LENGTH = 2;

ALWAYS_INLINE bool anu_file_path_exists (char *path) {
  struct stat statb;
  return (path && (stat(path, &statb) == 0)) != 0;
}

ALWAYS_INLINE bool anu_file_path_is_dir (char *path) {
  struct stat statb;
  return (path && stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
}

/* Check extension of filename */
int anu_file_ext_supported (char *path) {
  assert(path);
  char *dot = strrchr(path, '.');

  /* Check for '.' */
  if (!dot || dot == path) {
    return 0;
  }

  char file_ext_lower[5] = {0};

  /* Skip over the dot... */
  char *extension = dot + 1;

  int i = 0;
  for (; i < 5; i++) {
    char c = extension[i];
    /* Break if null terminator */
    if (c == '\0') {
      break;
    }
    /* Reached 5th character, extension is too long */
    if (i == VIDEO_EXTENSION_MAX_LENGTH) {
      return 0;
    }

    file_ext_lower[i] = (char) anu_util_tolower(c);
  }

  /* If i was less than two characters */
  if (i < VIDEO_EXTENSION_MIN_LENGTH) {
    return 0;
  }

  /* Search if extension is within array (binary searching) */
  int low = 0;
  int high = (int) VIDEO_EXTENSIONS_COUNT - 1;

  while (low <= high) {
    int mid = low + ((high - low) / 2);
    int cmp = strcmp(file_ext_lower, video_extensions[mid]);

    if (cmp == 0) {
      return 1;
    }
    if (cmp < 0) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }

  return 0;
}

/**
 * @brief Resolve a relative path.
 *
 * @param path[in] Path to resolve.
 * @param out[out] Buffer to place resolved path in.
 * @return Returns 0 on success, anything else on failure.
 **/
int anu_file_resolve_relative_path (char *restrict path, char *restrict out) {

  if (!path) {
    return -1;
  }

  char output[PATH_MAX];
  errno = 0;
  char *ptr = realpath(path, &output[0]);
  if (ptr == NULL) {
    perror("Error resolving path: ");
    return -1;
  }

  strcpy(out, output);

  return 0;
}

static ALWAYS_INLINE int anu_file_opendir (char *dir_path, DIR **out) {
  assert(dir_path && out);

  *out = opendir(dir_path);

  if (*out == NULL) {
    return 1;
  }
  /* printf("Opened directory: `%s`!\n", dir_path); */
  return 0;
}

/**
 * @brief Get a file name (extension included) from path.
 * @return Success: pointer to the start of the file name.
 * Failure: Returns path pointer.
 **/
char *anu_file_basename (char *path) {
  assert(path);

  char *start = strrchr(path, '/');

  return start ? (start + 1) : path;
}

/**
 * @brief Get filename, excluding the extension.
 **/
char *anu_file_basename_stem (char *path, char *out, size_t out_size) {

  assert(path);
  assert(out);
  assert(out_size > 0);

  /* Get files name */
  char *start = anu_file_basename(path);
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
static int FUNC_NONNULL_ALL
handle_path_pointing_to_file (char *path, anu_file_vec *files_out) {

  struct stat statb = {0};
  int stat_return = 0;
  errno = 0;
  stat_return = stat(path, &statb);
  if (stat_return && errno) {
    perror("Error running 'stat' on file: ");
    return -1;
  }

  anu_file file = {
    .ctime = statb.st_ctime,
    .mtime = statb.st_mtime,
    .size = (size_t) statb.st_size,
    .path = strdup(path),
  };

  char *base_ptr = anu_file_basename(path);
  if (base_ptr == path) {
    log_error("Could not determine basename from path...");
    return 1;
  }
  file.name_offset = (u32) (base_ptr - path);

  kv_push(*files_out, file);
  return 0;
}

void anu_file_vec_destroy (anu_file_vec *v) {

  for (size_t i = 0; i < kv_size(*v); i++) {
    anu_file *file = &kv_A(*v, i);
    /* Must free the dynamically allocated path strings */
    free(file->path);
  }

  kv_destroy(*v);
}

/**
 * @brief Recursively search path and return files found.
 **/
int FUNC_NONNULL_ALL anu_file_recursive_filewalk (char *path,
                                                  anu_file_vec *files_out) {

  if (!anu_file_path_exists(path)) {
    log_warn("%s not valid path.", path);
    return -1;
  }

  /* Check if we have received a path to a FILE with extension we support */
  if (anu_file_ext_supported(path)) {
    log_info("Received path for regular video file: %s", path);
    return handle_path_pointing_to_file(path, files_out);
  }

  /* Resolve path if needed */

  /* Stack to hold directories */
  kvec_t(char *) dirstack;
  kv_init(dirstack);
  /* Initialise with the path received */
  kv_push(dirstack, strdup(path));

  while (kv_size(dirstack) > 0) {

    char *curr_path = kv_pop(dirstack);
    ANU_ASSUME(curr_path != NULL);

    /* Directory stream */

    DIR *dir = opendir(curr_path);
    if (!dir) {
      log_warn("Could not open directory: %s", curr_path);
      free(curr_path);
      closedir(dir);
      continue;
    }

    int dir_fd = dirfd(dir);
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

      /* Ignore symlinks, sockets, devices, etc. */
      if ((dp->d_type != DT_REG) && (dp->d_type != DT_DIR) &&
          (dp->d_type != DT_UNKNOWN)) {
        continue;
      }

      /* Check path for supported extension */
      if (dp->d_type != DT_DIR && dp->d_type == DT_REG &&
          !anu_file_ext_supported(name)) {
        continue;
      }

      char *full_path;

      if (asprintf(&full_path, "%s/%s", curr_path, name) == -1) {
        log_error("Failed to allocate memory for path variable?");
        continue;
      }

      /* If its a directory push it to our directory stack */
      if (dp->d_type == DT_DIR) {
        kv_push(dirstack, full_path);
        continue;
      }

      /* Stat buffer */
      struct stat statb;

      /* Run stat on path */
      if (fstatat(dir_fd, dp->d_name, &statb, 0) != 0) {
        log_warn("Failed to fstatat file '%s'", full_path);
        free(full_path);
        continue;
      }
      if (statb.st_size == 0) {
        log_debug("File size is 0 '%s'", full_path);
        free(full_path);
        continue;
      }

      size_t name_len = strlen(name);

      anu_file newfile = {
        .size = (usize) statb.st_size,
        .ctime = statb.st_ctime,
        .mtime = statb.st_mtime,
        .ino = statb.st_ino,
        .dev = statb.st_dev,
        .path = full_path,
        .name_offset = (u32) (name_len + 1),
      };

      kv_push(*files_out, newfile);
    }
    closedir(dir);
    free(curr_path);
  }

  kv_destroy(dirstack);
  return 0;
}

/* Helper function for quick-sort
 * Compares 'a' with 'b' lexicographically using its ASCII values
 * e.g. a="Hello" , b="Hi"
 * (H - H) = 0
 * (e - i) --> (101 - 105) = -4 => 'b' is lexicographically before 'a'  */
static inline int compare_strings (const void *a, const void *b) {
  return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* TODO: Add a check for hard linked files (files with same inode number) */
void scan_dirs (anukrta_config *config, anu_file_vec *files) {

  /* Check if we need to scan current directory */
  if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_SCAN_CURR_DIR)) {

    char *resolved = realpath(".", NULL);
    if (!resolved) {
      ANU_DIE("Could not resolve current path.");
    }

    log_info("Scanning current directory (%s)", resolved);
    if (anu_file_recursive_filewalk(resolved, files)) {
      log_warn("Error searching for files in current directory.");
    }

    free(resolved);
    return;
  }

  /* If we're not scanning current dir, then paths_count should be non zero */
  assert(config->paths_count > 0);

  /* Array to hold resolved absolute paths */
  kvec_t(char *) real_paths;
  kv_init(real_paths);

  for (size_t i = 0; i < config->paths_count; i++) {
    /* realpath with NULL automatically allocates memory for the resolved path */
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
    kv_destroy(real_paths);
    return;
  }

  /* Sort paths lexicographically
   * so "/a/b" will be sorted before "/a/b/c"
   * We can then remove redundant paths */
  qsort((void *) real_paths.items, valid_paths, sizeof(char *),
        compare_strings);

  size_t unique_paths = 1;

  for (size_t i = 1; i < valid_paths; i++) {
    char *prev = kv_A(real_paths, (unique_paths - 1));
    char *current = kv_A(real_paths, i);
    size_t prev_len = strlen(prev);

    bool is_duplicate_or_subdir = false;

    /* Check if 'current' starts with 'prev' */
    if (strncmp(prev, current, prev_len) == 0) {

      /* Ensure it's an exact match or an actual subdirectory,
       * avoiding similar names (e.g. prev="/dir", curr="/dir-2")
       * Also handle cases where path is '/'
       */
      if ((current[prev_len] == '\0') || (current[prev_len] == '/') ||
          (prev_len == 1 && prev[0] == '/')) {
        is_duplicate_or_subdir = true;
      }
    }

    /* If we found a redundant path */
    if (is_duplicate_or_subdir) {
      log_debug(
          "Skipping overlapping or duplicate path: '%s' (covered by '%s')",
          current, prev);
      free(current); /* Free the redundant path */
    } else {
      kv_A(real_paths, unique_paths) = current; /* Keep the unique path */
      ++unique_paths;
    }
  }

  real_paths.size = unique_paths;

  /* Now we scan only the unique paths */
  for (size_t i = 0; i < unique_paths; i++) {

    char *path = kv_A(real_paths, i);
    if (anu_file_recursive_filewalk(path, files)) {
      log_warn("Error searching for files in '%s'", path);
    }
    /* Free path once we're done */
    free(path);
  }

  kv_destroy(real_paths);
}
