#include "stack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/*
 * Vector
 * ~~~~~~
 */

int anu_vector_init (anu_vector *v, size_t capacity, size_t elem_size) {
  assert(elem_size);

  v->capacity = (capacity > 0) ? capacity : 2;
  v->count = 0;
  v->_elem_size = elem_size;

  v->items = malloc(capacity * elem_size);

  if (!v->items) {
    perror("Memory allocation failed.");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

#define VECTOR_FULL(v) (((v)->capacity) == ((v)->count)) ? 1 : 0

int anu_vector_append (anu_vector *v, void *item) {
  assert(v);

  if (VECTOR_FULL(v)) {
    size_t new_capacity = (v->capacity > 0) ? (v->capacity * 2) : 4;
    assert(new_capacity > 0);
    assert(new_capacity > v->capacity);
    void *temp = realloc(v->items, v->_elem_size * new_capacity);
    if (!temp) {
      perror("Reallocation failed.");
      return EXIT_FAILURE;
    }
    v->items = temp;
    v->capacity = new_capacity;
  }

  void *target = (byte *) v->items + (v->count * v->_elem_size);
  memcpy(target, item, v->_elem_size);
  ++(v->count);
  return (int) v->count;
}

int anu_vector_get (anu_vector *v, size_t index, void *out) {

  assert(v && out);

  if (v->count == 0 || index >= v->count) {
    return -1;
  }

  void *item_ptr = (byte *) (v->items) + (index * v->_elem_size);

  memcpy(out, item_ptr, v->_elem_size);
  return 0;
}

int anu_vector_pop_end (anu_vector *v, void *out) {
  assert(v);

  if (v->count < 1) {
    return 0;
  }

  --(v->count);

  if (!out) {
    return (int) v->count;
  }
  void *item_ptr = (byte *) (v->items) + (v->count * v->_elem_size);

  memcpy(out, item_ptr, v->_elem_size);
  return (int) v->count;
}

size_t anu_vector_count (anu_vector *v) {
  assert(v);
  return v->count;
}

void anu_vector_destroy (anu_vector *v) {
  if (!v) {
    return;
  }

  if (v->items) {
    free(v->items);
    v->items = NULL;
  }
  v->capacity = 0;
  v->count = 0;
  v->_elem_size = 0;
}

/* TODO */
/* void anu_vector_for_all(anu_vector *v, void (*operation)(void *)) */

/*****************************************************************************/

/*
 * Stack
 * ~~~~~
 */

void anu_stack_init (anu_stack *s, size_t capacity, size_t elem_size) {

  assert(elem_size);

  size_t init_cap = capacity;

  if (capacity == 0) {
    init_cap = 4;
  }

  s->capacity = capacity;
  s->count = 0;
  s->elem_size = elem_size;

  s->items = malloc(init_cap * elem_size);
  if (!s->items) {
    perror("Memory allocation failed.");
    return;
  }
}

void anu_stack_push (anu_stack *s, void *item_ptr) {
  assert(s && s->capacity > 0);

  if (s->count == s->capacity) {
    size_t new_capacity = (s->capacity * 2);
    void *copied = realloc(s->items, (new_capacity * s->elem_size));

    if (copied == NULL) {
      perror("Reallocation failed.");
      return;
    }

    s->items = copied;
    s->capacity = new_capacity;
  }
  void *target_address = (char *) s->items + (s->count * s->elem_size);
  memcpy(target_address, item_ptr, s->elem_size);
  ++s->count;
}

int anu_stack_pop (anu_stack *s, void *dest) {
  if (s->count == 0) {
    return 0;
  }
  --s->count;
  void *source = (char *) s->items + (s->count * s->elem_size);
  memcpy(dest, source, s->elem_size);

  return 1;
}

int anu_stack_is_empty (anu_stack *s) { return (s->count == 0); }

void anu_stack_peek (anu_stack *s, void *dest) {
  if (anu_stack_is_empty(s)) {
    return;
  }
  void *source = (char *) s->items + ((s->count - 1) * s->elem_size);
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
