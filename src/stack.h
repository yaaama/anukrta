#ifndef ANU_STACK_H
#define ANU_STACK_H

#include <stddef.h>

typedef struct anu_stack {
  void *items;
  size_t count;
  size_t capacity;
  size_t elem_size;
} anu_stack;

void anu_stack_init(anu_stack *s, size_t capacity, size_t elem_size);
void anu_stack_push(anu_stack *s, void *item_ptr);
int anu_stack_pop(anu_stack *s, void *dest);
void anu_stack_destroy(anu_stack *s);

#endif  // ANU_STACK_H
