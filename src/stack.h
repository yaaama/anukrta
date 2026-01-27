#ifndef STACK_H_
#define STACK_H_

#include <stddef.h>

typedef struct {
  void* items;
  size_t count;
  size_t capacity;
  size_t elem_size;
} anuStack;

void anu_stack_init(anuStack* s, size_t capacity, size_t elem_size);
void anu_stack_push(anuStack* s, void* item_ptr);
int anu_stack_pop(anuStack* s, void* dest);
void anu_stack_destroy(anuStack* s);

#endif  // STACK_H_
