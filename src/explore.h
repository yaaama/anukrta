#ifndef EXPLORE_H_
#define EXPLORE_H_

#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef char* u8;

#define ANU_MAX_PATH_LEN 512

#if 0

typedef struct media_info {
  bool fill_later;
} media_info;

typedef struct {
  bool fill_later;
} media_flags;

#endif

typedef struct {
  /* Path */
  char path[ANU_MAX_PATH_LEN];
  /* Size in bytes */
  size_t size;
  long ctime;
  char name[256];
} anuFile;

typedef struct {
  anuFile* items;  /* Items */
  size_t count;    /* Items in storage */
  size_t capacity; /* Max capacity */
  size_t head;     /* Head index */
  size_t tail;     /* Tail index (next open slot) */
} anuFileQ;

void anu_fileq_init(anuFileQ* q, size_t init_capacity);
void anu_fileq_destroy(anuFileQ* q);
int anu_fileq_enqueue(anuFileQ* q, anuFile* file_in);
int anu_fileq_dequeue(anuFileQ* q, anuFile* file_out);
int anu_open_dir(char* dir_path, DIR** out);
size_t anu_recursive_filewalk(char* searchp, anuFileQ* files_out);
#endif  // EXPLORE_H_
