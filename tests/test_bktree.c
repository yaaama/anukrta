#include <criterion/assert.h>
#include <criterion/criterion.h>
#include <criterion/internal/assert.h>
#include <criterion/internal/test.h>
#include <criterion/logging.h>
#include <stdint.h>

#include "../src/hash.h"
#include "../src/tree.h"
#include "../src/util.h"

Test (BK_Tree, initialise) {
  bkTree tree = {0};
  bkTree_insert(&tree, 10000L, 1);
  bkTree_print_ascii(&tree);
  bkTreeNode_free(tree.root);
}

Test (BK_Tree, normal_insertions) {

  bkTree tree = {0};
  bkTree_insert(&tree, 0x2e4d99e444b1bf0e, 1);
  bkTree_insert(&tree, 0x68141b6d97eeb979, 2);
  bkTree_insert(&tree, 0x2e4d99e644b1bf0e, 3);
  bkTree_insert(&tree, 0x68141b6d97eeb979, 4);
  bkTree_insert(&tree, 0x730d6e55b27757b2, 6);
  bkTree_insert(&tree, 0x6e5b5b56536f6fff, 7);
  bkTree_insert(&tree, 0x36a796d9e3248399, 8);
  bkTree_insert(&tree, 0x0e6a9794b7a32cd4, 9);
  bkTree_insert(&tree, 0x2e4d99e44cb3bf0e, 10);
  bkTree_insert(&tree, 0x68141b6d97eeb979, 11);
  bkTree_insert(&tree, 0x2e4d99e44cb3bf0e, 12);
  bkTree_insert(&tree, 0x6759bbec41d3ac3d, 13);
  bkTree_insert(&tree, 0x0e6a9794b7a32cd4, 14);
  bkTree_insert(&tree, 500L, 15);
  bkTree_print_ascii(&tree);

  bkTreeNode_free(tree.root);
}

Test (BK_Tree, null_tree) {

  bkTree *tree = NULL;

  bkTree_insert(tree, 500L, 15);

}
