#include <criterion/criterion.h>
#include <string.h>

#include "../src/report.h"
#include "../src/tree.h"

TestSuite(Report, .description = "Tests for Report Generation and Formatting");

Test (Report, human_sizing_iec) {
  char buf[64];

  /* Test Byte output */
  get_human_sizing_iec(500, sizeof(buf), buf);
  cr_assert_str_eq(buf, "500 B");

  /* Test Exact Kibibyte */
  get_human_sizing_iec(1024, sizeof(buf), buf);
  cr_assert_str_eq(buf, "1.00 KiB");

  /* Test Fractions (e.g., 1.5 MiB = 1024 * 1024 * 1.5 = 1572864) */
  get_human_sizing_iec(1572864, sizeof(buf), buf);
  cr_assert_str_eq(buf, "1.50 MiB");

  /* Test large sizes */
  get_human_sizing_iec(1073741824ULL, sizeof(buf), buf); /* 1 GiB */
  cr_assert_str_eq(buf, "1.00 GiB");
}

Test (Report, generate_duplicate_groups) {
  /*
   * Simulate a scenario with 4 files:
   * File 0 and File 1 are identical (Distance 0).
   * File 2 is slightly different but within threshold of File 1 (Distance 1).
   * File 3 is completely unique.
   */

  anukrta_config config = {.segments = 1, .threshold = 2};

  anu_file_q files;
  anu_fileq_init(&files, 4);
  anu_file f0 = {.path = strdup("v0.mp4")};
  anu_file f1 = {.path = strdup("v1.mp4")};
  anu_file f2 = {.path = strdup("v2.mp4")};
  anu_file f3 = {.path = strdup("v3.mp4")};
  anu_fileq_enqueue(&files, &f0);
  anu_fileq_enqueue(&files, &f1);
  anu_fileq_enqueue(&files, &f2);
  anu_fileq_enqueue(&files, &f3);

  /* Setup the flat hashes array (1 segment per file) */
  uint64_t hashes[4] = {
    0x0000000000000000, /* File 0 */
    0x0000000000000000, /* File 1 */
    0x0000000000000001, /* File 2 */
    0xFFFFFFFFFFFFFFFF  /* File 3 */
  };

  /* Populate the BK-Tree manually */
  bk_tree tree = {0};
  for (size_t i = 0; i < 4; i++) {
    bk_tree_insert(&tree, hashes[i], i);
  }

  /* Generate the report using Union-Find */
  anu_report report = anu_generate_report(&files, hashes, &config, &tree);

  /*
   * Expected outcome:
   * Group 1: Files 0, 1, and 2 (transitive matching through threshold)
   * File 3 is isolated and should NOT appear in the report (groups must be > 1 item).
   */
  cr_assert_eq(report.count, 1, "There should be exactly 1 duplicate group");
  cr_assert_eq(report.groups[0].count, 3, "Group should contain 3 files");

  /* Verify the correct IDs are in the group (order doesn't matter, but based on your loop it's 0, 1, 2) */
  bool has_0 = false, has_1 = false, has_2 = false;
  for (size_t i = 0; i < 3; i++) {
    uint64_t id = report.groups[0].file_ids[i];
    if (id == 0) {
      has_0 = true;
    }
    if (id == 1) {
      has_1 = true;
    }
    if (id == 2) {
      has_2 = true;
    }
  }

  cr_assert(has_0 && has_1 && has_2,
            "Group should exactly contain File IDs 0, 1, and 2");

  /* Clean up */
  anu_report_destroy(&report);
  bk_tree_node_free(tree.root);

  /* Must dequeue and free paths to avoid memory leak in test */
  anu_file tmp;
  while (anu_fileq_dequeue(&files, &tmp)) {
    free(tmp.path);
  }
  anu_fileq_destroy(&files);
}
