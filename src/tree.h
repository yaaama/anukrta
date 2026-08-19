#ifndef ANU_BK_TREE_H
#define ANU_BK_TREE_H

#include <stddef.h>
#include <stdint.h>

#include "kvec.h"

/* Vector of file ids */
typedef kvec_t(uint64_t) u64_vec;

typedef struct bk_child_edge {
  struct bk_node *node;
  size_t distance;
} bk_child_edge;

typedef struct bk_node {
  u64_vec exact_dupe_file_ids;
  /* Children of node */
  bk_child_edge *children;
  uint64_t hash;
  size_t child_count;
  size_t child_capacity;
} bk_node;

/* Create new BK node */
bk_node *bk_tree_node_new(uint64_t hash, uint64_t file_id);

/* Search for nodes with hashes with distance less than tolerance */
void bk_tree_search(bk_node *root, uint64_t hash, size_t tolerance, u64_vec *groups_out);

/* Insert hash into tree */
void bk_tree_insert(bk_node **tree_ptr, uint64_t hash, uint64_t file_id);
/* Free node */
void bk_tree_node_free(bk_node *node);
void bk_tree_print_ascii(bk_node *tree);

#endif  // ANU_BK_TREE_H
