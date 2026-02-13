#ifndef TREE_H_
#define TREE_H_

#define BK_CHILD_ARR_SIZE 65
#define MAX_FILES_PER_NODE 16

#include <stddef.h>
#include <stdint.h>

typedef struct anuDupeGroup {
  uint64_t files[MAX_FILES_PER_NODE];
  size_t file_count;

} anuDupeGroup;

typedef struct bkNode {
  uint64_t hash;
  uint64_t file_ids[MAX_FILES_PER_NODE];
  int file_id_count;
  struct bkNode *children[65]; /* Children of node */
  size_t child_count;
} bkNode;

typedef struct bkTree {
  /* Root node */
  bkNode *root;
} bkTree;

/* Create new BK node */
bkNode *bkTreeNode_new(uint64_t hash, uint64_t file_id);

/* Search for nodes with hashes with distance less than tolerance */
void bkTree_search(bkNode *node, uint64_t hash, uint64_t tolerance,
                   anuDupeGroup *groups_out);

/* Insert hash into tree */
void bkTree_insert(bkTree *tree, uint64_t hash, uint64_t file_id);
/* Free node */
void bkTreeNode_free(bkNode *node);
void bkTree_print_ascii(bkTree *tree);

#endif  // TREE_H_
