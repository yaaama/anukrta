#ifndef ANU_BK_TREE_H
#define ANU_BK_TREE_H

#include "stack.h"
#define BK_CHILD_ARR_SIZE 65
#define MAX_FILES_PER_NODE 16

#include <stddef.h>
#include <stdint.h>

typedef struct bk_child_edge {
  int distance;
  struct bk_node *node;
} bk_child_edge;

typedef struct bk_node {
  uint64_t hash;
  anu_vector exact_dupe_file_ids;
  /* Children of node */
  bk_child_edge *children;
  int child_count;
  int child_capacity;
} bk_node;

typedef struct bk_tree {
  /* Root node */
  bk_node *root;
} bk_tree;

/* Create new BK node */
bk_node *bk_tree_node_new(uint64_t hash, uint64_t file_id);

/* Search for nodes with hashes with distance less than tolerance */
void bk_tree_search(bk_node *node,
                    uint64_t hash,
                    size_t tolerance,
                    anu_vector *groups_out);

/* Insert hash into tree */
void bk_tree_insert(bk_tree *tree, uint64_t hash, uint64_t file_id);
/* Free node */
void bk_tree_node_free(bk_node *node);
void bk_tree_print_ascii(bk_tree *tree);

#endif  // ANU_BK_TREE_H
