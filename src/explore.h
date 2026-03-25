#ifndef ANU_EXPLORE_H
#define ANU_EXPLORE_H

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "util.h"

typedef struct anu_file {
  /* Path */
  char *path;
  /* Size in bytes */
  size_t size;
  /* Duration of video file */
  size_t duration_us;
  /* File mode change time */
  long ctime;
  /* File modification time */
  long mtime;
  /* Index for when name starts in path */
  ptrdiff_t name;
} anu_file;

typedef struct anu_file_q {
  anu_file *items; /* Items */
  size_t count;    /* Items in storage */
  size_t capacity; /* Max capacity */
  size_t head;     /* Head index */
  size_t tail;     /* Tail index (next open slot) */
} anu_file_q;

void anu_fileq_init(anu_file_q *q, size_t init_capacity);
void anu_fileq_destroy(anu_file_q *q);
int anu_fileq_enqueue(anu_file_q *q, anu_file *file_in);
int anu_fileq_dequeue(anu_file_q *q, anu_file *file_out);

int anu_file_recursive_filewalk(char *path, anu_file_q *files_out);
int anu_file_opendir(char *dir_path, DIR **out);
int anu_file_ext_supported(char *filename);

static ALWAYS_INLINE char *anu_file_get_filename (anu_file *f) {
  return f->path + f->name;
}

bool anu_file_path_is_dir(char *path);
bool anu_file_path_exists(char *path);
int anu_file_resolve_relative_path(char *path, char *out);
char *anu_file_basename(char *path);
char *anu_file_basename_stem(char *path, char *out, size_t out_size);
#endif  // ANU_EXPLORE_H
