#ifndef EXPLORE_H_
#define EXPLORE_H_

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define ANU_MAX_PATH_LEN 512

typedef struct anu_file {
  /* Path */
  char path[ANU_MAX_PATH_LEN];
  /* Index for when name starts in path */
  int name;
  /* Size in bytes */
  size_t size;
  /* Creation time */
  long ctime;
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
#endif  // EXPLORE_H_
