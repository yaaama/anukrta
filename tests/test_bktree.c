#include <criterion/criterion.h>
#include <stdint.h>

#include "../src/tree.h"

Test (BK_Tree, initialise) {
  bk_node *tree = NULL;
  bk_tree_insert(&tree, 10000L, 1);
  /* bk_tree_print_ascii(&tree); */
  bk_tree_node_free(tree);
}

Test (BK_Tree, normal_insertions) {

  bk_node *tree = NULL;
  bk_node **tree_ptr = &tree;
  bk_tree_insert(tree_ptr, 0x2e4d99e444b1bf0e, 1);
  bk_tree_insert(tree_ptr, 0x68141b6d97eeb979, 2);
  bk_tree_insert(tree_ptr, 0x2e4d99e644b1bf0e, 3);
  bk_tree_insert(tree_ptr, 0x68141b6d97eeb979, 4);
  bk_tree_insert(tree_ptr, 0x730d6e55b27757b2, 6);
  bk_tree_insert(tree_ptr, 0x6e5b5b56536f6fff, 7);
  bk_tree_insert(tree_ptr, 0x36a796d9e3248399, 8);
  bk_tree_insert(tree_ptr, 0x0e6a9794b7a32cd4, 9);
  bk_tree_insert(tree_ptr, 0x2e4d99e44cb3bf0e, 10);
  bk_tree_insert(tree_ptr, 0x68141b6d97eeb979, 11);
  bk_tree_insert(tree_ptr, 0x2e4d99e44cb3bf0e, 12);
  bk_tree_insert(tree_ptr, 0x6759bbec41d3ac3d, 13);
  bk_tree_insert(tree_ptr, 0x0e6a9794b7a32cd4, 14);
  bk_tree_insert(tree_ptr, 500L, 15);
  /* bk_tree_print_ascii(&tree); */

  bk_tree_node_free(tree);
}

Test (BK_Tree, null_tree) {

  bk_node *tree = NULL;

  bk_tree_insert(&tree, 500L, 15);
}

Test (BK_Tree, search_tolerance) {
  bk_node *tree;

  /* Insert base hash (file 0) */
  tree = bk_tree_node_new(0x0000000000000000, 0);
  /* Distance of 1 from base (file 1) */
  bk_tree_insert(&tree, 0x0000000000000001, 1);
  /* Distance of 2 from base (file 2) */
  bk_tree_insert(&tree, 0x0000000000000003, 2);
  /* Insert hash with distance of 64 from base (file 3) */
  bk_tree_insert(&tree, 0xFFFFFFFFFFFFFFFF, 3);

  /* Vector to store resulting file IDs */
  anu_vector results;
  anu_vector_init(&results, 4, sizeof(uint64_t));

  /* Search for hashes with a tolerance of 1 */
  bk_tree_search(tree, 0x0000000000000000, 1, &results);

  /* We expect to find file 0 (dist 0) and file 1 (dist 1) */
  cr_assert_eq(results.count, 2, "Expected 2 results, got %zu", results.count);

  uint64_t *matched_files = (uint64_t *) results.items;
  bool found_file_0 = false;
  bool found_file_1 = false;

  for (size_t i = 0; i < results.count; i++) {
    if (matched_files[i] == 0) {
      found_file_0 = true;
    }
    if (matched_files[i] == 1) {
      found_file_1 = true;
    }
  }

  cr_assert(found_file_0, "File 0 should have been found in the search.");
  cr_assert(found_file_1, "File 1 should have been found in the search.");

  anu_vector_destroy(&results);
  bk_tree_node_free(tree);
}
