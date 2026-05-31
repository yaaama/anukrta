#ifndef ANU_STACK_H
#define ANU_STACK_H

#include <stddef.h>

typedef struct anu_stack {
  void *items;
  size_t count;
  size_t capacity;
  size_t elem_size;
} anu_stack;

typedef struct anu_vector {
  void *items;
  size_t count;
  size_t capacity;
  size_t _elem_size;
} anu_vector;

/* Initialise a vector that stores elem_size worth of data, with some specified capacity */
int anu_vector_init(anu_vector *v, size_t capacity, size_t elem_size);
/* Push an element to the end of the vector */
int anu_vector_append(anu_vector *v, void *item);
/* Pop an element from the end of the vector */
int anu_vector_pop_end(anu_vector *v, void *out);
/* Pop an element from the beginning of the vector */
/* int anu_vector_pop_start(anu_vector *v, void *out); */
/* Return number of elements in vector */
size_t anu_vector_count(anu_vector *v);
int anu_vector_extend(anu_vector *v, void *items, size_t count);

/* Check if vector is empty */
inline static int anu_vector_is_empty (anu_vector *v) {
  return (v->count == 0);
}

/* Destroy vector, and all of its items */
void anu_vector_destroy(anu_vector *v);
/* Get item in vector at index */
int anu_vector_get(anu_vector *v, size_t index, void *out);
/* TODO: Perform an operation on all items in vector */
void anu_vector_for_all(anu_vector *v, void (*operation)(void *));

void anu_stack_init(anu_stack *s, size_t capacity, size_t elem_size);
void anu_stack_push(anu_stack *s, void *item_ptr);
int anu_stack_pop(anu_stack *s, void *dest);
void anu_stack_destroy(anu_stack *s);
int anu_stack_is_empty(anu_stack *s);
void anu_stack_peek(anu_stack *s, void *dest);

#endif  // ANU_STACK_H
