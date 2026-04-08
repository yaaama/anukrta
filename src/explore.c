/**
 * explore.c
 *
 * Searches paths recursively to retrieve files to analyse
 **/
#include "explore.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "log.h"
#include "stack.h"
#include "util.h"

void anu_fileq_init (anu_file_q *q, size_t init_capacity) {
  assert(init_capacity);
  if (!q) {
    return;
  }
  q->capacity = init_capacity;
  q->count = 0;
  q->items = calloc(q->capacity, sizeof(anu_file));
  q->head = 0;
  q->tail = 0;
}

int anu_fileq_enqueue (anu_file_q *q, anu_file *file_in) {

  /* should return an error */
  if ((!file_in) || (!q)) {
    return -1;
  }

  if (q->capacity <= q->count) {
    q->capacity *= 2;
    anu_file *temp = realloc(q->items, (sizeof(anu_file) * q->capacity));
    if (!temp) {
      /* probs out of mem */
      return -1;
    }
    q->items = temp;
    q->head = 0;
    q->tail = q->count;
  }

  q->items[q->count] = *file_in;
  q->tail = (q->tail + 1) % q->capacity;
  ++q->count;

  return 0;
}

int anu_fileq_dequeue (anu_file_q *q, anu_file *file_out) {

  if (!q || q->count == 0 || !file_out) {
    return 0;
  }

  *file_out = q->items[q->head];
  q->head = (q->head + 1) % q->capacity;
  q->count--;
  return 1;
}

void anu_fileq_destroy (anu_file_q *q) {

  if (!q || !q->items) {
    return;
  }

  for (size_t i = 0; i < q->count; i++) {
    anu_file *item = &q->items[i];
    free(item->path);
  }
  free(q->items);
}

struct anu_dir_job {
  char path[PATH_MAX];
};

/* Video extensions */
static const char *video_extensions[] = {
  "3g2", "3gp",  "amv",  "asf", "avi", "f4a",  "f4b", "f4p", "f4v", "flv",
  "flv", "gifv", "m4p",  "m4v", "m4v", "mkv",  "mng", "mod", "mov", "mp2",
  "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf",  "nsv", "ogg", "ogv", "qt",
  "rm",  "roq",  "rrc",  "svi", "vob", "webm", "wmv", "yuv"};

static const size_t VIDEO_EXTENSIONS_COUNT = ANU_ARRAY_SIZE(video_extensions);

ALWAYS_INLINE bool anu_file_path_exists (char *path) {
  struct stat statb;
  return (path && (stat(path, &statb) == 0)) != 0;
}

ALWAYS_INLINE bool anu_file_path_is_dir (char *path) {
  struct stat statb;
  return (path && stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
}

/* Check extension of filename */
int anu_file_ext_supported (char *filename) {
  assert(filename);
  char *dot = strrchr(filename, '.');

  /* Check for '.' */
  if (!dot || dot == filename) {
    return 0;
  }

  char file_ext_lower[8] = {0};

  /* Skip over the dot... */
  ++dot;
  char *extension = dot;

  /* Length of filename extension */
  size_t ext_len = strlen(extension);

  /* All supported extensions are either 3 or 4 characters long */
  if (ext_len < 3 || ext_len > 4) {
    return 0;
  }

  memcpy(file_ext_lower, extension, ext_len);
  file_ext_lower[ext_len] = '\0';

  int u = 0;
  while (file_ext_lower[u] != '\0') {
    file_ext_lower[u] = (char) anu_util_tolower(file_ext_lower[u]);
    ++u;
  }

  /* Search if extension is within array */
  for (size_t i = 0; i < VIDEO_EXTENSIONS_COUNT; i++) {
    if (strcmp(file_ext_lower, video_extensions[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/* TODO Resolve tilde into absolute path */
ALWAYS_INLINE static int anu_resolve_tilde (char *path) {

  if (!path) {
    return -1;
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

ALWAYS_INLINE static int anu_file_opendir (char *dir_path, DIR **out) {
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
static int handle_path_pointing_to_file (char *path, anu_file_q *files_out) {

  struct stat statb = {0};
  int stat_return = 0;
  errno = 0;
  stat_return = stat(path, &statb);
  if (stat_return && errno) {
    perror("Error running 'stat' on file: ");
    return -1;
  }

  anu_file file = {0};
  file.ctime = statb.st_ctime;
  file.mtime = statb.st_mtime;
  assert(statb.st_size > 0);
  file.size = (size_t) statb.st_size;

  file.path = realpath(path, NULL);
  char *base_ptr = anu_file_basename(path);
  assert(base_ptr != path);
  ptrdiff_t idx = (base_ptr - path) > 0 ? (base_ptr - path) : 0;
  assert(idx > 0);
  file.name = (size_t) idx;

  anu_fileq_enqueue(files_out, &file);
  return 0;
}

ALWAYS_INLINE static size_t filename_index (char *path) {

  assert(path);

  char *last_slash = strrchr(path, '/');

  if (!last_slash) {
    return 0;
  }

  /* last_slash points to '/'. */
  /* We want the character AFTER the slash. */
  /* Subtract pointers: (End Address) - (Start Address) = Index */
  return (size_t) ((last_slash + 1) - path);
}

/**
 * @brief Recursively search path and return files found.
 **/
int anu_file_recursive_filewalk (char *path, anu_file_q *files_out) {

  /* Check if path given actually exists */
  if (!anu_file_path_exists(path)) {
    return -1;
  }

  /* Resolve our root path just ONCE */
  char root_path[PATH_MAX];

  if (realpath(path, root_path) == NULL) {
    perror("Failed to resolve path:");
    return -1;
  }

  /* Stat buffer */
  struct stat statb = {0};
  /* Return value of calling stat on a file */
  int stat_return = 0;

  size_t file_len = strlen(root_path);

  /* Check if we have received a path to a FILE with extension we support */
  if (anu_file_ext_supported(root_path)) {
    log_info("Received path for regular video file: %s", root_path);
    return handle_path_pointing_to_file(root_path, files_out);
  }

  /* Initialise first directory we will explore */
  struct anu_dir_job dirjob;

  /* Temp var to hold the directory we are currently in */
  struct anu_dir_job currjob;

  memcpy(dirjob.path, root_path, file_len);
  dirjob.path[file_len] = '\0';

  /* Stack containing directories to visit
   * This lets us go through file hierarchies 'recursively'
   * without actually using recursion in the implementation */
  anu_stack dirstack;
  /* Initialise stack */
  anu_stack_init(&dirstack, 8, sizeof(struct anu_dir_job));
  /* Push first item into stack (root directory) */
  anu_stack_push(&dirstack, &dirjob);

  while (anu_stack_pop(&dirstack, &currjob)) {

    /* Directory stream */
    DIR *dir;

    /* Open directory for reading */
    if (anu_file_opendir(currjob.path, &dir) != 0) {
      log_warn("Could not open directory: %s", currjob.path);
      continue;
    };

    /* Dir entry */
    struct dirent *dp;
    /* Directory file descriptor */
    int dir_fd = dirfd(dir);
    if (dir_fd == -1) {
      perror("Could not get file descriptor for directory.");
      continue;
    }

    /* Path of current file */
    /* NOTE: We oversize fullpath because:
     * - We could end up with len of PATH_MAX + NAME_MAX + 1 characters
     * where +1 accounts for the null terminator */
    char fullpath[PATH_MAX];
    size_t base_len = strlen(currjob.path);
    memcpy(fullpath, currjob.path, base_len);
    fullpath[base_len] = '/';
    char *name_ptr = fullpath + base_len + 1;  // Pointer to where filename goes
    size_t max_name_len = PATH_MAX - base_len - 1;

    log_trace("Reading directory: %s", currjob.path);

    while ((dp = readdir(dir)) != NULL) {

      /* Skip over '.' and '..' */
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
        log_trace("Skipping DOT path: '%s'", dp->d_name);
        continue;
      }

      /* Ignore symlinks, sockets, devices, etc. */
      if (dp->d_type != DT_REG && dp->d_type != DT_UNKNOWN &&
          dp->d_type != DT_DIR) {
        log_trace("Skipping over NON REGULAR PATH: %s", dp->d_name);
        continue;
      }

      size_t name_len = strlen(dp->d_name);

      /* Prevents buffer overflow */
      if (UNLIKELY(name_len > max_name_len)) {
        continue;
      }
      /* +1 to copy the \0 terminator */
      memcpy(name_ptr, dp->d_name, name_len + 1);

      /* If its a directory */
      if (dp->d_type == DT_DIR) {
        memcpy(dirjob.path, fullpath, base_len + 1 + name_len + 1);
        anu_stack_push(&dirstack, &dirjob);
        continue;
      }

      /* Check path for supported extension */
      if (!anu_file_ext_supported(dp->d_name)) {
        log_trace("Skipping non supported extension: %s", dp->d_name);
        continue;
      }

      /* Run stat on path */
      if (fstatat(dir_fd, dp->d_name, &statb, 0) == -1) {
        perror("Stat failed: ");
        continue;
      }
      ASSUME(statb.st_size > 0);

      anu_file newfile = {0};
      /* Prepare newfile for data */
      newfile.size = (size_t) statb.st_size;
      newfile.ctime = statb.st_ctime;
      newfile.mtime = statb.st_mtime;

      /* Copy path */
      size_t total_path_len = base_len + 1 + name_len;
      newfile.path = malloc(total_path_len + 1);
      if (newfile.path == NULL) {
        ANU_DIE("Out of memory.");
      }
      memcpy(newfile.path, fullpath, total_path_len + 1);
      newfile.name = base_len + 1;  // Index where the filename starts

      if (anu_fileq_enqueue(files_out, &newfile)) {
        ANU_DIE("Out of memory.");
      }
    }
    closedir(dir);
  }

  anu_stack_destroy(&dirstack);
  return 0;
}
