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
  char path[ANU_MAX_PATH_LEN];
  /* Index for when name starts in path */
  int name;
  /* Size in bytes */
  size_t size;
  /* File mode change time */
  long ctime;
  /* File modification time */
  long mtime;
  long duration_us;
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
int anu_open_dir(char *dir_path, DIR **out);
int anu_recursive_filewalk(char *searchp, anu_file_q *files_out);

char *anu_file_get_filename(anu_file *f);
#endif  // ANU_EXPLORE_H
