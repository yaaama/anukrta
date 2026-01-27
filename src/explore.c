/**
 * explore.c
 *
 * Searches paths recursively to retrieve files to analyse
 **/
#include "explore.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ANU_MAX_PATH_LEN 512

typedef struct {
  char path[512];
} anuDirJob;

typedef struct {
  void* items;
  size_t count;
  size_t capacity;
  size_t elem_size;
} anuStack;

void anu_stack_init (anuStack* s, size_t capacity, size_t elem_size) {
  s->items = malloc(capacity * elem_size);
  s->capacity = capacity;
  s->count = 0;
  s->elem_size = elem_size;
}

void anu_stack_push (anuStack* s, void* item_ptr) {

  if (s->count == s->capacity) {
    s->capacity = (s->capacity * 2);
    void** copied = (void**)realloc(s->items, (s->capacity * s->elem_size));
    if (copied == NULL) {
      perror("Reallocation failed.");
      exit(0);
    }
    s->items = (void*)copied;
  }
  void* target_address = (char*)s->items + (s->count * s->elem_size);
  memcpy(target_address, item_ptr, s->elem_size);
  ++s->count;
}

int anu_stack_pop (anuStack* s, void* dest) {
  if (s->count == 0) {
    return 0;
  }
  --s->count;
  void* source = (char*)s->items + (s->count * s->elem_size);
  memcpy(dest, source, s->elem_size);

  return 1;
}

void anu_stack_destroy (anuStack* s) {
  free(s->items);
  s = NULL;
}

int anu_open_dir(char* dir_path, DIR** out);
int anu_files_in_path(DIR** dir, struct dirent** filelist_out);

/* Video extensions */
const char* VIDEO_EXTENSIONS[] = {
    "3g2", "3gp",  "amv",  "asf", "avi", "f4a",  "f4b", "f4p", "f4v", "flv",
    "flv", "gifv", "m4p",  "m4v", "m4v", "mkv",  "mng", "mod", "mov", "mp2",
    "mp4", "mpe",  "mpeg", "mpg", "mpv", "mxf",  "nsv", "ogg", "ogv", "qt",
    "rm",  "roq",  "rrc",  "svi", "vob", "webm", "wmv", "yuv", NULL};

int anu_open_dir (char* dir_path, DIR** out) {

  *out = opendir(dir_path);

  if (*out == NULL) {
    perror("Could not open directory.");
    return 1;
  }
  /* printf("Opened directory: `%s`!\n", dir_path); */
  return 0;
}

/* Check extension of filename */
int extension_is_supported (const char* filename) {
  assert(filename);
  const char* dot = strrchr(filename, '.');

  if (!dot || dot == filename) {
    return 0;
  }

  char file_ext_lower[8];

  /* Skip over the dot... */
  ++dot;

  strncpy(file_ext_lower, dot, 7);
  file_ext_lower[7] = '\0';

  /* Lowercase all the characters */
  for (int i = 0; file_ext_lower[i]; i++) {
    file_ext_lower[i] = (char)tolower(file_ext_lower[i]);
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
int anu_resolve_tilde (char* path) { return 0; }

int anu_recursive_filewalk (char* searchp) {

  /* Initialise first directory we will explore */
  anuDirJob dirjob;
  strncpy(dirjob.path, searchp, ANU_MAX_PATH_LEN);

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
  char fullpath[ANU_MAX_PATH_LEN];
  /* Files found counter */
  size_t files_found = 0;

  while (anu_stack_pop(&dirstack, &currjob)) {

    /* Open directory for reading */
    if (anu_open_dir(currjob.path, &dir)) {
      printf("Could not open directory: %s\n", currjob.path);
      continue;
    };

    while ((dp = readdir(dir)) != NULL) {

      /* Skip over '.' and '..' */
      if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
        continue;
      }

      if (snprintf(fullpath, ANU_MAX_PATH_LEN, "%s/%s", currjob.path,
                   dp->d_name) >= ANU_MAX_PATH_LEN) {
        continue;
      }
      stat_return = stat(fullpath, &statb);
      /* Handle stat errors here... */
      if (stat_return) {
        printf("We've encountered an error with this file: %s", fullpath);
        continue;
      }

      /* If its a directory */
      if (S_ISDIR(statb.st_mode)) {
        /* printf("Directory found: %s\n", fullpath); */
        strncpy(dirjob.path, fullpath, ANU_MAX_PATH_LEN);
        anu_stack_push(&dirstack, &dirjob);
      }

      /* Else if its a regular file */
      else if (S_ISREG(statb.st_mode)) {
        if (extension_is_supported(fullpath)) {
          /* printf("%s :: %zu\n", fullpath, files_found); */
          printf("%s\n", fullpath);
          ++files_found;
        }
      }
    }

    closedir(dir);
  }

  printf("Files found: %zu\n", files_found);

  anu_stack_destroy(&dirstack);
  return 0;
}
