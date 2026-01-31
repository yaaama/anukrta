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

void anu_fileq_init (anuFileQ* q, size_t init_capacity) {
  assert(init_capacity);
  if (!q) {
    return;
  }
  q->capacity = init_capacity;
  q->count = 0;
  q->items = (anuFile*)calloc(q->capacity, sizeof(anuFile));
  q->head = 0;
  q->tail = 0;
}

int anu_fileq_enqueue (anuFileQ* q, anuFile* file_in) {

  /* should return an error */
  if ((!file_in) || (!q)) {
    return -1;
  }

  if (q->capacity <= q->count) {
    q->capacity *= 2;
    anuFile* temp =
        (anuFile*)realloc(q->items, (sizeof(anuFileQ) * q->capacity));
    if (!temp) {
      /* probs out of mem */
      abort();
    }
    free((void*)q->items);
    q->items = temp;
    q->head = 0;
    q->tail = q->count;
  }

  q->items[q->count] = *file_in;
  q->tail = (q->tail + 1) % q->capacity;
  ++q->count;

  return 0;
}

int anu_fileq_dequeue (anuFileQ* q, anuFile* file_out) {

  if (!q || q->count == 0 || !file_out) {
    return 0;
  }

  *file_out = q->items[q->head];
  q->head = (q->head + 1) % q->capacity;
  q->count--;
  return 1;
}

void anu_fileq_destroy (anuFileQ* q) {

  if (!q || !q->items) {
    return;
  }

  free(q->items);
  q = NULL;
}

int anu_files_in_path(DIR** dir, struct dirent** filelist_out);

typedef struct {
  char path[512];
} anuDirJob;

/* Video extensions */
const char* VIDEO_EXTENSIONS[] = {
    "3g2", "3gp",  "amv",  "asf", "avi", "f4a",  "f4b", "f4p", "f4v", "flv",
    "flv", "gifv", "m4p",  "m4v", "m4v", "mkv",  "mng", "mod", "mov", "mp2",
    "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf",  "nsv", "ogg", "ogv", "qt",
    "rm",  "roq",  "rrc",  "svi", "vob", "webm", "wmv", "yuv", NULL};

const size_t VIDEO_EXTENSIONS_COUNT =
    (sizeof(VIDEO_EXTENSIONS) / sizeof(VIDEO_EXTENSIONS[0]));

/* Check extension of filename */
int anu_file_ext_supported (const char* filename) {
  assert(filename);
  const char* dot = strrchr(filename, '.');

  if (!dot || dot == filename) {
    return 0;
  }

  char file_ext_lower[8];

  /* Skip over the dot... */
  const char* extension = ++dot;

  size_t ext_len = strlen(extension);

  /* Check if extension length is between 4 chars and 3 */
  if (ext_len < 2 || ext_len > 4) {
    return 0;
  }

  strncpy(file_ext_lower, dot, 7);
  file_ext_lower[7] = '\0';

  /* Lowercase all the characters */
  for (int i = 0; file_ext_lower[i]; i++) {
    file_ext_lower[i] = (char)anu_util_tolower(file_ext_lower[i]);
  }

  /* Search if extension is within array */
  for (int i = 0; VIDEO_EXTENSIONS[i] != NULL; i++) {
    if (strcmp(file_ext_lower, VIDEO_EXTENSIONS[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/* TODO Resolve tilde into absolute path */
int anu_resolve_tilde (char* path) {

  if (!path) {
    return -1;
  }

  return 0;
}

int anu_open_dir (char* dir_path, DIR** out) {

  *out = opendir(dir_path);

  if (*out == NULL) {
    perror("Could not open directory.");
    return 1;
  }
  /* printf("Opened directory: `%s`!\n", dir_path); */
  return 0;
}

int anu_recursive_filewalk (char* searchp, anuFileQ* files_out) {

  /* Initialise first directory we will explore */
  anuDirJob dirjob;
  strncpy(dirjob.path, searchp, ANU_MAX_PATH_LEN);
  dirjob.path[ANU_MAX_PATH_LEN - 1] = '\0';

  /* Stack containing directories to visit */
  anuStack dirstack;
  anu_stack_init(&dirstack, 50, sizeof(anuDirJob));
  anu_stack_push(&dirstack, &dirjob);

  /* Temp var to hold the directory we are currently in */
  anuDirJob currjob;
  /* Directory stream */
  DIR* dir;
  /* Dir entry */
  struct dirent* dp;
  /* Stat buffer */
  struct stat statb;
  /* Return value of calling stat on file */
  int stat_return = 0;
  /* Path of current file */
  char fullpath[ANU_MAX_PATH_LEN] = {0};
  /* Files found counter */
  size_t files_found = 0;
  anuFile newfile = {0};
  while (anu_stack_pop(&dirstack, &currjob)) {

    /* Open directory for reading */
    if (anu_open_dir(currjob.path, &dir)) {
      fprintf(stderr, "Could not open directory: %s\n", currjob.path);
      continue;
    };

    while ((dp = readdir(dir)) != NULL) {

      /* Skip over '.' and '..' */
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
        continue;
      }

      if ((snprintf(fullpath, ANU_MAX_PATH_LEN, "%s/%s", (currjob.path),
                    dp->d_name)) >= ANU_MAX_PATH_LEN) {
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
        memset(&newfile, 0, sizeof(anuFile));

        newfile.size = statb.st_size;
        newfile.ctime = statb.st_ctime;
        memcpy(newfile.name, dp->d_name, 256);
        memcpy(newfile.path, fullpath, ANU_MAX_PATH_LEN);
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
