#ifndef ANU_BK_TREE_H
#define ANU_BK_TREE_H

#define BK_CHILD_ARR_SIZE 65
#define MAX_FILES_PER_NODE 16

#include <stddef.h>
#include <stdint.h>

typedef struct anu_dupe_group {
  uint64_t files[MAX_FILES_PER_NODE];
  size_t file_count;

} anu_dupe_group;

typedef struct bk_node {
  uint64_t hash;
  int exact_dupe_count;
  uint64_t exact_dupe_file_ids[BK_CHILD_ARR_SIZE];
  /* Children of node, indexed by distance */
  struct bk_node *children[65];
  size_t child_count;
} bk_node;

typedef struct bk_tree {
  /* Root node */
  bk_node *root;
} bk_tree;

/* Create new BK node */
bk_node *bk_tree_node_new(uint64_t hash, uint64_t file_id);

/* Search for nodes with hashes with distance less than tolerance */
void bk_tree_search(bk_node *node, uint64_t hash, uint64_t tolerance,
                    anu_dupe_group *groups_out);

/* Insert hash into tree */
void bk_tree_insert(bk_tree *tree, uint64_t hash, uint64_t file_id);
/* Free node */
void bk_tree_node_free(bk_node *node);
void bk_tree_print_ascii(bk_tree *tree);

#endif  // ANU_BK_TREE_H
