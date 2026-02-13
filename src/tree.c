
#include "tree.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

bkNode *bkTreeNode_new (uint64_t hash, uint64_t file_id) {

  bkNode *node = calloc(1, sizeof(bkNode));

  if (!node) {
    abort();
  }
  node->file_id_count = 0;
  node->hash = hash;
  node->file_ids[0] = file_id;
  node->file_id_count = 1;

  return node;
}

// NOLINTBEGIN (*recursion)
void bkTreeNode_free (bkNode *node) {
  // NOLINTEND
  if (!node) {
    return;
  }

  for (int i = 0; i < BK_CHILD_ARR_SIZE; i++) {
    if (node->children[i]) {
      bkTreeNode_free(node->children[i]);
    }
  }

  free(node);
}


// NOLINTBEGIN (*recursion)
static void bkTree_insert_internal (bkNode *node, uint64_t hash,
                                    uint64_t file_id) {
  // NOLINTEND
  uint64_t dist = hamming_distance(node->hash, hash);

  if (!dist) {
    /* Exact match (collision). Add data to this node. */
    node->file_ids[node->file_id_count] = file_id;
    ++node->file_id_count;
    return;
  }

  /* Traverse down the tree */
  if (!node->children[dist]) {
    /* Create new child here */
    node->children[dist] = bkTreeNode_new(hash, file_id);
  } else {
    /* Recurse */
    bkTree_insert_internal(node->children[dist], hash, file_id);
  }
}

void bkTree_insert (bkTree *tree, uint64_t hash, uint64_t file_id) {
  if (!tree) {
    return;
  }
  if (!tree->root) {
    tree->root = bkTreeNode_new(hash, file_id);
  } else {
    bkTree_insert_internal(tree->root, hash, file_id);
  }
}

// NOLINTBEGIN (*recursion)
void bkTree_search (bkNode *node, uint64_t hash, uint64_t tolerance,
                    anuDupeGroup *groups_out) {
  // NOLINTEND

  uint64_t distance = hamming_distance(node->hash, hash);

  if (distance <= tolerance) {

    for (int k = 0; k < node->file_id_count; k++) {
      groups_out->files[groups_out->file_count] = node->file_ids[k];
      ++groups_out->file_count;
    }
  }
  uint64_t min_search = 1;
  uint64_t max_search = distance + tolerance;

  if (tolerance < distance) {
    min_search = distance - tolerance;
  }

  if (max_search > 64) {
    max_search = 64;
  }

  for (uint64_t i = min_search; i <= max_search; i++) {
    if (node->children[i]) {
      bkTree_search(node->children[i], hash, tolerance, groups_out);
    }
  }
}

// NOLINTBEGIN (*recursion)
static void bkNode_print_recursive (bkNode *node, int depth,
                                    int edge_distance) {
  // NOLINTEND
  if (!node) {
    return;
  }

  /* Print Indentation */
  for (int i = 0; i < depth; i++) {
    printf(i == depth - 1 ? "|__ " : "    ");
  }

  /* Print Edge Weight (Distance from parent) and Node Info
     If edge_distance is -1, it's the root. */
  if (edge_distance != -1) {
    printf("[%d] ", edge_distance);
  } else {
    printf("[ROOT] ");
  }

  printf("Hash: %016lx | Files: ", node->hash);

  for (int i = 0; i < node->file_id_count; i++) {
    printf("%ld ", node->file_ids[i]);
  }
  printf("\n");

  /* Recurse children
     We iterate 1 to 64 because distance 0 is the node itself (handled in
     file_ids) */
  for (int i = 1; i < BK_CHILD_ARR_SIZE; i++) {
    if (node->children[i]) {
      bkNode_print_recursive(node->children[i], depth + 1, i);
    }
  }
}

void bkTree_print_ascii (bkTree *tree) {

  if (!tree || !tree->root) {
    printf("(Empty Tree)\n");
    return;
  }
  printf("\n--- BK Tree Structure ---\n");
  bkNode_print_recursive(tree->root, 0, -1);
  printf("-------------------------\n");
}
