/**
 * explore.c
 *
 * Searches paths recursively to retrieve files to analyse
 **/
#include "explore.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "stack.h"
#include "util.h"

void anu_fileq_init (anu_file_q *q, size_t init_capacity) {
  assert(init_capacity);
  if (!q) {
    return;
  }
  q->capacity = init_capacity;
  q->count = 0;
  q->items = (anu_file *)calloc(q->capacity, sizeof(anu_file));
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
      exit(EXIT_FAILURE);
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

  free(q->items);
}

int anu_files_in_path(DIR **dir, struct dirent **filelist_out);

char *anu_file_get_filename (anu_file *f) { return f->path + f->name; }

struct anu_dir_job {
  char path[512];
};

/* Video extensions */
static const char *video_extensions[] = {
    "3g2", "3gp",  "amv",  "asf", "avi", "f4a",  "f4b", "f4p", "f4v", "flv",
    "flv", "gifv", "m4p",  "m4v", "m4v", "mkv",  "mng", "mod", "mov", "mp2",
    "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf",  "nsv", "ogg", "ogv", "qt",
    "rm",  "roq",  "rrc",  "svi", "vob", "webm", "wmv", "yuv", NULL};

static const size_t VIDEO_EXTENSIONS_COUNT =
    (sizeof(video_extensions) / sizeof(video_extensions[0]));

/* Check extension of filename */
static int anu_file_ext_supported (const char *filename) {
  assert(filename);
  const char *dot = strrchr(filename, '.');

  if (!dot || dot == filename) {
    return 0;
  }

  char file_ext_lower[8] = {0};

  /* Skip over the dot... */
  const char *extension = ++dot;

  size_t ext_len = strlen(extension);

  /* Check if extension length is between 4 chars and 3 */
  if (ext_len < 2 || ext_len > 4) {
    return 0;
  }

  memcpy(file_ext_lower, extension, ext_len);
  file_ext_lower[ext_len] = '\0';

  int u = 0;
  while (file_ext_lower[u] != '\0') {
    file_ext_lower[u] = (char)anuUtil_tolower(file_ext_lower[u]);
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

int anu_open_dir (char *dir_path, DIR **out) {

  *out = opendir(dir_path);

  if (*out == NULL) {
    perror("Could not open directory.");
    return 1;
  }
  /* printf("Opened directory: `%s`!\n", dir_path); */
  return 0;
}

int anu_recursive_filewalk (char *searchp, anu_file_q *files_out) {

  /* Initialise first directory we will explore */
  struct anu_dir_job dirjob;

  /* Temp var to hold the directory we are currently in */
  struct anu_dir_job currjob;

  size_t file_len = strlen(searchp);
  memcpy(dirjob.path, searchp, file_len);
  dirjob.path[file_len] = '\0';

  /* Stack containing directories to visit */
  anu_stack dirstack;
  anu_stack_init(&dirstack, 20, sizeof(struct anu_dir_job));
  anu_stack_push(&dirstack, &dirjob);

  /* Directory stream */
  DIR *dir;
  /* Dir entry */
  struct dirent *dp;
  /* Stat buffer */
  struct stat statb;
  /* Return value of calling stat on a file */
  int stat_return = 0;
  /* Path of current file */
  char fullpath[ANU_MAX_PATH_LEN] = {0};
  /* Files found counter */
  size_t files_found = 0;
  anu_file newfile;

  while (anu_stack_pop(&dirstack, &currjob)) {

    /* Open directory for reading */
    if (anu_open_dir(currjob.path, &dir) != 0) {
      fprintf(stderr, "Could not open directory: %s\n", currjob.path);
      continue;
    };

    while ((dp = readdir(dir)) != NULL) {

      /* Skip over '.' and '..' */
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
        continue;
      }

      int path_length = snprintf(fullpath, ANU_MAX_PATH_LEN, "%s/%s",
                                 (currjob.path), dp->d_name);

      if (path_length > ANU_MAX_PATH_LEN) {
        continue;
      }

      stat_return = stat(fullpath, &statb);
      /* Handle stat errors here... */
      if (stat_return) {
        fprintf(stderr, "Stat failed for `%s`: ", fullpath);
        perror("");
        continue;
      }

      /* If its a directory */
      if (S_ISDIR(statb.st_mode)) {
        /* printf("Directory found: %s\n", fullpath); */
        strncpy(dirjob.path, fullpath, ANU_MAX_PATH_LEN);
        anu_stack_push(&dirstack, &dirjob);
        continue;
      }

      /* Else if its a regular file */
      if (S_ISREG(statb.st_mode)) {

        if (!anu_file_ext_supported(fullpath)) {
          continue;
        }
        /* printf("%s :: %zu\n", fullpath, files_found); */
        /* printf("%s\n", fullpath); */

        /* Prepare newfile for data */
        newfile.size = statb.st_size;
        newfile.ctime = statb.st_ctime;
        /* Copy path */
        memcpy(newfile.path, fullpath, path_length);
        newfile.path[path_length] = '\0';

        /* Find the last slash so we can extract the name */
        char *last_slash = strrchr(newfile.path, '/');

        if (last_slash) {
          /* last_slash points to '/'. */
          /* We want the character AFTER the slash. */
          /* Subtract pointers: (End Address) - (Start Address) = Index */
          newfile.name = (int)((last_slash + 1) - newfile.path);
        } else {
          /* No slash, default to index 0. */
          newfile.name = 0;
        }
        anu_fileq_enqueue(files_out, &newfile);
        ++files_found;
      }
    }

    closedir(dir);
  }

  printf("Files found: %zu\n", files_found);

  anu_stack_destroy(&dirstack);
  return 0;
}
