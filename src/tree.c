
#include "tree.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "stack.h"
#include "util.h"

bk_node *bk_tree_node_new (uint64_t hash, uint64_t file_id) {

  bk_node *node = calloc(1, sizeof(bk_node));

  if (!node) {
    return NULL;
  }

  node->hash = hash;
  node->children = NULL;
  node->child_count = 0;
  node->child_capacity = 0;

  anu_vector_init(&node->exact_dupe_file_ids, 1, sizeof(node->hash));
  anu_vector_append(&node->exact_dupe_file_ids, &file_id);

  return node;
}

// NOLINTBEGIN (*recursion)
void bk_tree_node_free (bk_node *node) {
  // NOLINTEND
  if (!node) {
    return;
  }

  for (int i = 0; i < node->child_count; i++) {
    bk_tree_node_free(node->children[i].node);
  }

  if (node->children) {
    free(node->children);
  }

  anu_vector_destroy(&node->exact_dupe_file_ids);
  free(node);
}

// NOLINTBEGIN (*recursion)
static void bkTree_insert_internal (bk_node *node,
                                    uint64_t hash,
                                    uint64_t file_id) {
  // NOLINTEND
  int dist = hamming_distance(node->hash, hash);

  /* If this is not true, then something horrible has gone wrong. */
  ASSUME(dist >= 0);

  if (!dist) {
    /* Exact match (collision). Add data to this node. */
    anu_vector_append(&node->exact_dupe_file_ids, &file_id);
    return;
  }

  for (int i = 0; i < node->child_count; i++) {
    if (node->children[i].distance == dist) {
      /* Found child with this distance
       * Recurse down this child. */
      bkTree_insert_internal(node->children[i].node, hash, file_id);
      return;
    }
  }

  /* Traverse down the tree */
  /* 2. Edge not found. We must create a new child branch. */
  /* Check capacity and grow the array if necessary */
  if (node->child_count == node->child_capacity) {
    int new_cap = (node->child_capacity == 0) ? 2 : (node->child_capacity * 2);
    bk_child_edge *temp =
        realloc(node->children, (size_t) new_cap * sizeof(bk_child_edge));
    if (!temp) {
      ANU_DIE("Failed to allocate memory for BK Tree edges.");
    }
    node->children = temp;
    node->child_capacity = new_cap;
  }

  assert(node->child_count >= 0);
  node->children[node->child_count].distance = dist;
  node->children[node->child_count].node = bk_tree_node_new(hash, file_id);
  ++node->child_count;
}

void bk_tree_insert (bk_node **tree_ptr, uint64_t hash, uint64_t file_id) {
  assert(tree_ptr);
  if (*tree_ptr == NULL) {
    *tree_ptr = bk_tree_node_new(hash, file_id);
    return;
  }

  bkTree_insert_internal(*tree_ptr, hash, file_id);
}

// NOLINTBEGIN (*recursion)
void bk_tree_search (bk_node *node,
                     uint64_t hash,
                     size_t tolerance,
                     anu_vector *groups_out) {
  // NOLINTEND
  if (!node) {
    return;
  }

  int distance_int = hamming_distance(node->hash, hash);
  assert(distance_int >= 0 && distance_int <= 64);

  size_t distance = (size_t) distance_int;

  /* Found a match */
  if (distance <= tolerance) {
    uint64_t *file_ids = (uint64_t *) node->exact_dupe_file_ids.items;

    for (size_t k = 0; k < node->exact_dupe_file_ids.count; k++) {
      anu_vector_append(groups_out, &file_ids[k]);
    }
  }

  int min_search = (distance > tolerance) ? (distance - tolerance) : 0;
  int max_search = MINIMUM((distance + tolerance), 64);

  for (int i = 0; i < node->child_count; i++) {
    size_t edge_within_tolerance =
        (((size_t) node->children[i].distance >= min_search) &&
         ((size_t) node->children[i].distance <= max_search));

    if (edge_within_tolerance) {
      bk_tree_search(node->children[i].node, hash, tolerance, groups_out);
    }
  }
}

// NOLINTBEGIN (*recursion)
static void bk_node_print_recursive (bk_node *node,
                                     int depth,
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
  uint64_t *items = (uint64_t *) node->exact_dupe_file_ids.items;

  for (size_t i = 0; i < node->exact_dupe_file_ids.count; i++) {
    uint64_t hashitem = items[i];
    printf(" %" PRIu64 ",", hashitem);
  }
  printf("]\n");

  /* Recurse children
     We iterate 1 to 64 because distance 0 is the node itself (handled in
     file_ids) */
  for (int i = 0; i < node->child_count; i++) {
    bk_node_print_recursive(node->children[i].node, depth + 1,
                            node->children[i].distance);
  }
}

void bk_tree_print_ascii (bk_node *tree) {

  if (!tree) {
    printf("(Empty Tree)\n");
    return;
  }
  printf("\n--- BK Tree Structure ---\n");
  bk_node_print_recursive(tree, 0, -1);
  printf("-------------------------\n");
}
