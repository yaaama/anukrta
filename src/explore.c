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
