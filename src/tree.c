
#include "tree.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kvec.h"
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

  for (size_t i = 0; i < node->child_count; i++) {
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
  uint64_t node_hash = node->hash;
  unsigned int hamming_dist = hamming_distance(node_hash, hash);
  /* If this is not true, then something horrible has gone wrong. */
  ANU_ASSUME(hamming_dist <= 64);
  size_t dist = (size_t) hamming_dist;

  if (!dist) {
    /* Exact match (collision). Add data to this node. */
    anu_vector_append(&node->exact_dupe_file_ids, &file_id);
    return;
  }

  for (size_t i = 0; i < node->child_count; i++) {
    if (node->children[i].distance == dist) {
      /* Found child with this distance
       * Recurse down this child. */
      bkTree_insert_internal(node->children[i].node, hash, file_id);
      return;
    }
  }

  /* Traverse down the tree */
  /* Edge not found. We must create a new child branch. */

  /* Check capacity and grow the array if necessary */
  if (node->child_count == node->child_capacity) {
    size_t new_cap =
        (node->child_capacity > 0) ? (node->child_capacity * 2) : 2;
    assert(new_cap > node->child_capacity);
    bk_child_edge *temp =
        realloc(node->children, new_cap * sizeof(bk_child_edge));
    if (!temp) {
      ANU_DIE("Failed to allocate memory for BK Tree edges.");
    }
    node->children = temp;
    node->child_capacity = new_cap;
  }

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

typedef kvec_withinit_t(const bk_node *, 32) bk_node_stack;

void bk_tree_search (bk_node *root,
                     uint64_t hash,
                     size_t tolerance,
                     bk_search_results *groups_out) {

  assert(tolerance <= 64);

  if (!root) {
    return;
  }

  const ptrdiff_t tol = (ptrdiff_t) tolerance;

  bk_node_stack stack;
  kvi_init(stack);
  kvi_push(stack, root);

  while (kv_size(stack) > 0) {
    const bk_node *node = kv_pop(stack);
    ptrdiff_t distance = (ptrdiff_t) hamming_distance(node->hash, hash);

    ANU_ASSUME(distance >= 0 && distance <= 64);

    /* Found a match */
    if (distance <= tol) {
      kvi_concat_len(*groups_out, node->exact_dupe_file_ids.items,
                     node->exact_dupe_file_ids.count);
    }

    ptrdiff_t min_search = distance - tol;
    ptrdiff_t max_search = distance + tol;

    for (size_t i = 0; i < node->child_count; i++) {
      ptrdiff_t d = (ptrdiff_t) node->children[i].distance;
      if (d >= min_search && d <= max_search) {
        kvi_push(stack, node->children[i].node);
      }
    }
  }
  kvi_destroy(stack);  // Clean up
}

// NOLINTBEGIN (*recursion)
static void bk_node_print_recursive (bk_node *node,
                                     usize depth,
                                     size edge_distance) {
  // NOLINTEND
  if (UNLIKELY(!node)) {
    return;
  }

  /* Print Indentation */
  for (usize i = 0; i < depth; i++) {
    printf(i == depth - 1 ? "|__ " : "    ");
  }

  /* Print Edge Weight (Distance from parent) and Node Info
     If edge_distance is -1, it's the root. */
  if (edge_distance != -1) {
    printf("[%td] ", edge_distance);
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
  for (size_t i = 0; i < node->child_count; i++) {
    ANU_ASSUME(node->children[i].distance <= 64);
    bk_node_print_recursive(node->children[i].node, depth + 1,
                            (size) node->children[i].distance);
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
