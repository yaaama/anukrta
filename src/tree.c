
#include "tree.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

bk_node *bk_tree_node_new (uint64_t hash, uint64_t file_id) {

  bk_node *node = calloc(1, sizeof(bk_node));

  if (!node) {
    exit(EXIT_FAILURE);
  }
  node->exact_dupe_count = 0;
  node->hash = hash;
  node->exact_dupe_file_ids[0] = file_id;
  node->exact_dupe_count = 1;

  return node;
}

// NOLINTBEGIN (*recursion)
void bk_tree_node_free (bk_node *node) {
  // NOLINTEND
  if (!node) {
    return;
  }

  for (int i = 0; i < BK_CHILD_ARR_SIZE; i++) {
    if (node->children[i]) {
      bk_tree_node_free(node->children[i]);
    }
  }

  free(node);
}

// NOLINTBEGIN (*recursion)
static void bkTree_insert_internal (bk_node *node, uint64_t hash,
                                    uint64_t file_id) {
  // NOLINTEND
  uint64_t dist = hamming_distance(node->hash, hash);

  if (!dist) {
    /* Exact match (collision). Add data to this node. */
    node->exact_dupe_file_ids[node->exact_dupe_count] = file_id;
    ++node->exact_dupe_count;
    return;
  }

  /* Traverse down the tree */
  if (!node->children[dist]) {
    /* Create new child here */
    node->children[dist] = bk_tree_node_new(hash, file_id);
  } else {
    /* Recurse */
    bkTree_insert_internal(node->children[dist], hash, file_id);
  }
}

void bk_tree_insert (bk_tree *tree, uint64_t hash, uint64_t file_id) {
  if (!tree) {
    return;
  }
  if (!tree->root) {
    tree->root = bk_tree_node_new(hash, file_id);
  } else {
    bkTree_insert_internal(tree->root, hash, file_id);
  }
}

// NOLINTBEGIN (*recursion)
void bk_tree_search (bk_node *node, uint64_t hash, i32 tolerance,
                     anu_dupe_group *groups_out) {
  // NOLINTEND
  if (tolerance < 0) {
    return;
  }

  i32 distance = hamming_distance(node->hash, hash);

  /* Found a match */
  if (distance <= tolerance) {
    for (int k = 0; k < node->exact_dupe_count; k++) {
      size_t next_group_idx = groups_out->file_count;
      uint64_t *match = &groups_out->files[next_group_idx];
      *match = node->exact_dupe_file_ids[k];
      groups_out->file_count++;
    }
  }

  i32 min_search = distance - tolerance;
  i32 max_search = distance + tolerance;

  if (min_search < 0) {
    min_search = 0;
  }
  if (max_search > 64) {
    max_search = 64;
  }

  for (i32 i = min_search; i <= max_search; i++) {
    if (node->children[i]) {
      bk_tree_search(node->children[i], hash, tolerance, groups_out);
    }
  }
}

// NOLINTBEGIN (*recursion)
static void bk_node_print_recursive (bk_node *node, int depth,
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

  printf("Hash: %016lX | Files: [", node->hash);

  for (int i = 0; i < node->exact_dupe_count; i++) {
    printf(" %lu,", node->exact_dupe_file_ids[i]);
  }
  printf("]\n");

  /* Recurse children
     We iterate 1 to 64 because distance 0 is the node itself (handled in
     file_ids) */
  for (int i = 1; i < BK_CHILD_ARR_SIZE; i++) {
    if (node->children[i]) {
      bk_node_print_recursive(node->children[i], depth + 1, i);
    }
  }
}

void bk_tree_print_ascii (bk_tree *tree) {

  if (!tree || !tree->root) {
    printf("(Empty Tree)\n");
    return;
  }
  printf("\n--- BK Tree Structure ---\n");
  bk_node_print_recursive(tree->root, 0, -1);
  printf("-------------------------\n");
}
