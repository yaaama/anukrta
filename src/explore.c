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
  char path[512];
};

/* Video extensions */
static const char *video_extensions[] = {
  "3g2", "3gp",  "amv",  "asf", "avi", "f4a",  "f4b", "f4p", "f4v", "flv",
  "flv", "gifv", "m4p",  "m4v", "m4v", "mkv",  "mng", "mod", "mov", "mp2",
  "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf",  "nsv", "ogg", "ogv", "qt",
  "rm",  "roq",  "rrc",  "svi", "vob", "webm", "wmv", "yuv"};

static const size_t VIDEO_EXTENSIONS_COUNT = ANU_ARRAY_SIZE(video_extensions);

bool anu_file_path_exists (char *path) {
  struct stat statb;
  return (path && stat(path, &statb) == 0) != 0;
}

bool anu_file_path_is_dir (char *path) {
  struct stat statb;
  return (path && stat(path, &statb) == 0 && S_ISDIR(statb.st_mode)) != 0;
}

/* Check extension of filename */
int anu_file_ext_supported (char *filename) {
  assert(filename);
  char *dot = strrchr(filename, '.');

  if (!dot || dot == filename) {
    return 0;
  }

  char file_ext_lower[8] = {0};

  /* Skip over the dot... */
  char *extension = ++dot;

  size_t ext_len = strlen(extension);

  /* Check if extension length is between 4 chars and 3 */
  if (ext_len < 2 || ext_len > 4) {
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
static int anu_resolve_tilde (char *path) {

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
int anu_file_resolve_relative_path (char *path, char *out) {

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

int anu_file_opendir (char *dir_path, DIR **out) {
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
 * @brief Recursively search path and return files found.
 **/
int anu_file_recursive_filewalk (char *path, anu_file_q *files_out) {

  /* Check if path given actually exists */
  if (!anu_file_path_exists(path)) {
    return -1;
  }

  /* Stat buffer */
  struct stat statb = {0};
  /* Return value of calling stat on a file */
  int stat_return = 0;

  size_t file_len = strlen(path);

  /* Check if we have received a path to a FILE with extension we support */
  if (!anu_file_path_is_dir(path) && anu_file_ext_supported(path)) {
    log_info("Received path for regular video file: %s", path);

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
    file.name = (base_ptr - path) > 0 ? (int) (base_ptr - path) : 0;

    anu_fileq_enqueue(files_out, &file);
    return 0;
  }

  /* Initialise first directory we will explore */
  struct anu_dir_job dirjob;

  /* Temp var to hold the directory we are currently in */
  struct anu_dir_job currjob;

  memcpy(dirjob.path, path, file_len);
  dirjob.path[file_len] = '\0';

  /* Stack containing directories to visit
   * This lets us go through file hierarchies 'recursively'
   * without actually using recursion in the implementation */
  anu_stack dirstack;
  anu_stack_init(
      &dirstack, 4,
      sizeof(struct anu_dir_job));     // Initialise stack to being capacity 4
  anu_stack_push(&dirstack, &dirjob);  // Push the root directory into the stack

  /* Directory stream */
  DIR *dir;
  /* Dir entry */
  struct dirent *dp;
  /* Path of current file */
  char fullpath[PATH_MAX] = {0};
  anu_file newfile = {0};

  while (anu_stack_pop(&dirstack, &currjob)) {

    /* Open directory for reading */
    if (anu_file_opendir(currjob.path, &dir) != 0) {
      log_warn("Could not open directory: %s", currjob.path);
      continue;
    };

    while ((dp = readdir(dir)) != NULL) {

      /* Skip over '.' and '..' */
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
        continue;
      }

      unsigned long currjob_path_len = strlen(currjob.path);

      /* If dir path ends with trailing slash, lets null terminate it before we combine it with the filename
       * E.g. without this, then:
       * etc/directory -> etc/directory//video.mp4 */
      if (currjob.path[currjob_path_len - 1] == '/') {
        currjob.path[currjob_path_len - 1] = '\0';
        --currjob_path_len;
      }

      /* Combine directory path with filename */
      int path_length = snprintf(fullpath, ANU_MAX_PATH_LEN, "%s/%s",
                                 (currjob.path), dp->d_name);

      if (path_length > ANU_MAX_PATH_LEN) {
        log_warn("Path length (%d) exceeds max length: %d", path_length,
                 ANU_MAX_PATH_LEN);
        continue;
      }

      stat_return = stat(fullpath, &statb);
      /* Handle stat errors here... */
      if (stat_return == -1) {
        perror("Stat failed: ");
        continue;
      }

      /* If its a directory */
      if (S_ISDIR(statb.st_mode)) {
        /* printf("Directory found: %s\n", fullpath); */
        strcpy(dirjob.path, fullpath);
        anu_stack_push(&dirstack, &dirjob);
        continue;
      }

      /* Else if its a regular file */
      if (S_ISREG(statb.st_mode)) {

        if (!anu_file_ext_supported(fullpath)) {
          continue;
        }

        /* Prepare newfile for data */
        assert(statb.st_size > 0);
        newfile.size = (size_t) statb.st_size;
        newfile.ctime = statb.st_ctime;
        newfile.mtime = statb.st_mtime;
        /* Copy path */
        assert(path_length > 0);
        newfile.path = realpath(fullpath, NULL);

        /* Find the last slash so we can extract the name */
        char *last_slash = strrchr(newfile.path, '/');

        if (last_slash) {
          /* last_slash points to '/'. */
          /* We want the character AFTER the slash. */
          /* Subtract pointers: (End Address) - (Start Address) = Index */
          newfile.name = (int) ((last_slash + 1) - newfile.path);
        } else {
          /* No slash, default to index 0. */
          newfile.name = 0;
        }
        anu_fileq_enqueue(files_out, &newfile);
      }
    }

    closedir(dir);
  }

  anu_stack_destroy(&dirstack);
  return 0;
}

char *anu_file_get_filename (anu_file *f) { return f->path + f->name; }
