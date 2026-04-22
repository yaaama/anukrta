#ifndef ANU_BK_TREE_H
#define ANU_BK_TREE_H

#include <stddef.h>
#include <stdint.h>

#include "stack.h"

typedef struct bk_child_edge {
  struct bk_node *node;
  int distance;
} bk_child_edge;

typedef struct bk_node {
  anu_vector exact_dupe_file_ids;
  /* Children of node */
  bk_child_edge *children;
  uint64_t hash;
  int child_count;
  int child_capacity;
} bk_node;

/* Create new BK node */
bk_node *bk_tree_node_new(uint64_t hash, uint64_t file_id);

/* Search for nodes with hashes with distance less than tolerance */
void bk_tree_search(bk_node *node,
                    uint64_t hash,
                    int tolerance,
                    anu_vector *groups_out);

/* Insert hash into tree */
void bk_tree_insert(bk_node **tree_ptr, uint64_t hash, uint64_t file_id);
/* Free node */
void bk_tree_node_free(bk_node *node);
void bk_tree_print_ascii(bk_node *tree);

#endif  // ANU_BK_TREE_H
