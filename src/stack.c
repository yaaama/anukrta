#include "stack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void anu_stack_init (anu_stack *s, size_t capacity, size_t elem_size) {

  if (capacity == 0) {
    capacity = 4;
  }

  s->items = malloc(capacity * elem_size);
  if (!s->items) {
    perror("Memory allocation failed.");
    exit(EXIT_FAILURE);
  }
  s->capacity = capacity;
  s->count = 0;
  s->elem_size = elem_size;
}

void anu_stack_push (anu_stack *s, void *item_ptr) {

  if (s->count == s->capacity) {
    size_t new_capacity = (s->capacity * 2);
    void *copied = realloc(s->items, (new_capacity * s->elem_size));

    if (copied == NULL) {
      perror("Reallocation failed.");
      exit(EXIT_FAILURE);
    }

    s->items = copied;
    s->capacity = new_capacity;
  }
  void *target_address = (char *)s->items + (s->count * s->elem_size);
  memcpy(target_address, item_ptr, s->elem_size);
  ++s->count;
}

int anu_stack_pop (anu_stack *s, void *dest) {
  if (s->count == 0) {
    return 0;
  }
  --s->count;
  void *source = (char *)s->items + (s->count * s->elem_size);
  memcpy(dest, source, s->elem_size);

  return 1;
}

int anu_stack_is_empty (anu_stack *s) { return (s->count == 0); }

void anu_stack_peek (anu_stack *s, void *dest) {
  if (anu_stack_is_empty(s)) {
    return;
  }
  void *source = (char *)s->items + ((s->count - 1) * s->elem_size);
  memcpy(dest, source, s->elem_size);
}

void anu_stack_destroy (anu_stack *s) {
  if (s->items) {
    free(s->items);
    s->items = NULL;
  }
  s->count = 0;
  s->capacity = 0;
}
