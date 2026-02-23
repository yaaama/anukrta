#include <criterion/criterion.h>
#include <stdint.h>

#include "../src/tree.h"

Test (BK_Tree, initialise) {
  bk_tree tree = {0};
  bk_tree_insert(&tree, 10000L, 1);
  /* bk_tree_print_ascii(&tree); */
  bk_tree_node_free(tree.root);
}

Test (BK_Tree, normal_insertions) {

  bk_tree tree = {0};
  bk_tree_insert(&tree, 0x2e4d99e444b1bf0e, 1);
  bk_tree_insert(&tree, 0x68141b6d97eeb979, 2);
  bk_tree_insert(&tree, 0x2e4d99e644b1bf0e, 3);
  bk_tree_insert(&tree, 0x68141b6d97eeb979, 4);
  bk_tree_insert(&tree, 0x730d6e55b27757b2, 6);
  bk_tree_insert(&tree, 0x6e5b5b56536f6fff, 7);
  bk_tree_insert(&tree, 0x36a796d9e3248399, 8);
  bk_tree_insert(&tree, 0x0e6a9794b7a32cd4, 9);
  bk_tree_insert(&tree, 0x2e4d99e44cb3bf0e, 10);
  bk_tree_insert(&tree, 0x68141b6d97eeb979, 11);
  bk_tree_insert(&tree, 0x2e4d99e44cb3bf0e, 12);
  bk_tree_insert(&tree, 0x6759bbec41d3ac3d, 13);
  bk_tree_insert(&tree, 0x0e6a9794b7a32cd4, 14);
  bk_tree_insert(&tree, 500L, 15);
  /* bk_tree_print_ascii(&tree); */

  bk_tree_node_free(tree.root);
}

Test (BK_Tree, null_tree) {

  bk_tree *tree = NULL;

  bk_tree_insert(tree, 500L, 15);
}
