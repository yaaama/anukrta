#include "report.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "explore.h"
#include "stack.h"
#include "tree.h"
#include "util.h"

/* Finds the representative (or "root") of the set containing element 'i'
 *Implements path compression for efficiency. */
static size_t find_set (size_t i, size_t *parent) {
  if (parent[i] == i) {
    return i;
  }
  /* Path compression: set parent directly to the root */
  return parent[i] = find_set(parent[i], parent);
}

/* Merges the sets containing elements 'i' and 'j' */
static void unite_sets (size_t i, size_t j, size_t *parent) {
  size_t root_i = find_set(i, parent);
  size_t root_j = find_set(j, parent);
  if (root_i != root_j) {
    /* Make the root of 'i's set a child of the root of 'j's set */
    parent[root_i] = root_j;
  }
}

void anu_print_report (anu_report *report, anu_file_q *files) {
  if (report->count == 0) {
    printf("\n=== Report ===\n");
    printf("No duplicate groups found.\n");
    return;
  }

  printf("\n=== Duplicate Report: ===\n");
  printf("Found %zu duplicate groups from %zu files\n", report->count,
         files->count);
  printf("----------------------------------------");

  for (size_t i = 0; i < report->count; i++) {
    anu_duplicate_group *group = &report->groups[i];
    printf("\n[+] Group #%zu (%zu items):\n", i + 1, group->count);
    for (size_t j = 0; j < group->count; j++) {
      size_t file_id = group->file_ids[j];
      char *filename = files->items[file_id].path;
      printf("\t- %s\n", filename);
    }
  }
}

void anu_report_destroy (anu_report *report) {
  if (!report) {
    return;
  }
  for (size_t i = 0; i < report->count; i++) {
    free(report->groups[i].file_ids);
  }
  free(report->groups);
}

anu_report anu_generate_report (anu_file_q *files, uint64_t *hashes,
                                anukrta_config *config, bk_tree *tree) {
  size_t file_count = files->count;
  anu_report report = {0};
  report.count = 0;
  if (file_count == 0) {
    return report;
  }
  bk_tree_print_ascii(tree);

  /* Union-Find to identify the groups */
  size_t *parent = malloc((file_count) * sizeof(size_t));
  if (!parent) {
    exit(EXIT_FAILURE);
  }

  /* Initially, each file is in its own set */
  for (size_t i = 0; i < file_count; i++) {
    parent[i] = i;
  }

  for (size_t i = 0; i < file_count; i++) {
    /* Important to zero-initialize */
    anu_dupe_group segment_results = {0};

    for (int seg = 0; seg < config->segments; seg++) {
      uint64_t current_hash = hashes[(i * config->segments) + seg];
      bk_tree_search(tree->root, current_hash, config->threshold,
                     &segment_results);
    }

    for (size_t k = 0; k < segment_results.file_count; k++) {
      size_t match_id = segment_results.files[k];
      assert(match_id < file_count);
      /* Merge the sets of the file and its match */
      unite_sets(i, match_id, parent);
    }
  }

  /* Convert the Union-Find result into a list of groups */

  /* Initial capacity of report */
  report.capacity = 10;
  report.groups = calloc(report.capacity, sizeof(anu_duplicate_group));
  if (!report.groups) {
    exit(EXIT_FAILURE);
  }

  /* Use a temporary array of stacks/dynamic arrays to bucket the files by their
  root parent */
  anu_vector *buckets = calloc((file_count), sizeof(anu_vector));
  if (!buckets) {
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < file_count; i++) {
    size_t root = find_set(i, parent);
    /* Should never happen if logic is correct */
    assert(root < file_count);
    if (!buckets[root].items) {
      anu_vector_init(&buckets[root], 4, sizeof(uint64_t));
    }
    anu_vector_append(&buckets[root], &i);
  }

  /* NOTE: If a bucket has more than one file, it's a duplicate group */
  /* Populate final report struct */
  for (size_t i = 0; i < file_count; i++) {

    /* Destroy any buckets with less than 1 file */
    if (buckets[i].count <= 1) {
      anu_vector_destroy(&buckets[i]);
      continue;
    }

    /* Check if we have reached report capcity before filling it */
    if (report.count == report.capacity) {
      report.capacity *= 2;
      anu_duplicate_group *temp =
          realloc(report.groups, report.capacity * sizeof(anu_duplicate_group));
      if (!temp) {
        exit(EXIT_FAILURE);
      }
      report.groups = temp;
    }

    anu_duplicate_group *new_group = &report.groups[report.count++];
    new_group->count = buckets[i].count;
    /* Steal the memory from the stack */
    new_group->file_ids = buckets[i].items;
  }

  free(buckets);
  free(parent);
  return report;
}
