#include "report.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "explore.h"
#include "stack.h"
#include "tree.h"

#ifdef ANU__USE_RECURSIVE_SET_FIND  // Recursive 'find_set' implementation
#  pragma message("Making use of recursive `find_set` implementation.")

/* Finds the representative (or "root") of the set containing element 'i'
 *Implements path compression for efficiency. */
static size_t find_set (size_t i, size_t *parent) {
  if (parent[i] == i) {
    return i;
  }
  /* Path compression: set parent directly to the root */
  return parent[i] = find_set(parent[i], parent);
}
#else
static size_t find_set (size_t i, size_t *parent) {
  size_t root = i;

  /* Pass 1: Find the actual root */
  while (parent[root] != root) {
    root = parent[root];
  }

  /* Pass 2: Path compression
   * Traverse the path again, making every node point to the root */
  while (parent[i] != root) {
    size_t next_node = parent[i];
    parent[i] = root;
    i = next_node;
  }

  return root;
}
#endif  // ANU__USE_RECURSIVE_SET_FIND

/* Merges the sets containing elements 'i' and 'j' */
static void unite_sets (size_t i, size_t j, size_t *parent) {
  size_t root_i = find_set(i, parent);
  size_t root_j = find_set(j, parent);

  if (root_i != root_j) {
    /* Make the root of 'i's set a child of the root of 'j's set */
    parent[root_i] = root_j;
  }
}

static const char *units_iec[] = {"B", "KiB", "MiB", "GiB", "TiB"};

char *get_human_sizing_iec (u64 n_bytes, size_t buf_size, char *buf) {

  const int num_units = ANU_ARRAY_SIZE(units_iec);
  int unit_index = 0;
  /* We will use this to keep track of the fractional part */
  size_t remainder = 0;

  /* >> 10 is equivalent to dividing by 1024 */
  while ((n_bytes >= 1024) && (unit_index < (num_units - 1))) {
    remainder = n_bytes & 1023; /* Equivalent to: n_bytes % 1024 */
    n_bytes >>= 10;             /* Equivalent to: n_bytes / 1024 */
    ++unit_index;
  }
  int c;
  if (unit_index == 0) {
    c = sprintf(buf, "%" PRIu64 " %s", n_bytes, units_iec[unit_index]);
  } else {
    /* Calculate the 2-digit decimal part using pure integer math.
     * We multiply the remainder by 100, then divide by 1024 (by shifting).
     * This gives us a perfectly safe 0-99 value. */
    size_t decimals = (remainder * 100) >> 10;

    c = sprintf(buf, "%" PRIu64 ".%02zu %s", n_bytes, decimals,
                units_iec[unit_index]);
  }

  assert(c > 0);
  return buf;
}

static char *get_date_from_epoch (time_t *epoch_time,
                                  size_t buf_size,
                                  char *buf) {
  struct tm timeinfo = {0};
  localtime_r(epoch_time, &timeinfo);

  size_t ret = strftime(buf, buf_size, "%d-%m-%Y %H:%M", &timeinfo);
  assert(ret > 0);
  return buf;
}

void anu_print_report (anukrta_config *config,
                       anu_report *report,
                       anu_file_q *files,
                       u64 *hashes) {

  printf("\n=== Duplicate Report: ===\n");
  if (report->count == 0) {
    printf("No duplicate groups found.\n");
    return;
  }

  printf("Found %zu duplicate groups from %zu files\n", report->count,
         files->count);
  printf("----------------------------------------");
  printf("----------------------------------------\n");

  for (size_t i = 0; i < report->count; i++) {
    dupe_group_vector *group = &report->groups[i];

    printf("\n[+] Group #%zu (%zu items):\n", i + 1, group->count);
    for (size_t j = 0; j < group->count; j++) {
      size_t file_id = group->file_ids[j];
      anu_file *file = &files->items[file_id];
      char human_sizing[32] = {0};
      get_human_sizing_iec(file->size, sizeof(human_sizing), human_sizing);
      /* time_t change_t = file->ctime; */
      time_t modification_t = file->mtime;
      char time_str[64] = {0};
      get_date_from_epoch(&modification_t, sizeof(time_str), time_str);
      printf("  %s", file->path);
      printf("\n");

      printf("\t-> [%zu] | size: %-10s | time: %-15s | duration: %-.2fs\n",
             file_id, human_sizing, time_str,
             anu_time_microseconds_to_seconds(file->duration_us));

      printf("\t-> Hashes: [ ");
      for (size_t seg = 0; seg < config->segments; seg++) {
        /* Calculate the exact index in the flat array */
        uint64_t hash_val = hashes[(file_id * config->segments) + seg];

        /* Print as zero-padded 16-character uppercase Hex */
        printf("%016" PRIX64 " ", hash_val);
      }
      printf("]\n");
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

anu_report anu_generate_report (anu_file_q *files,
                                uint64_t *hashes,
                                anukrta_config *config,
                                bk_tree *tree) {
  size_t file_count = files->count;
  anu_report report = {0};

  if (file_count == 0 || tree == NULL) {
    return report;
  }

  report.count = 0;

  /* Union-Find to identify the groups */
  size_t *parent = malloc(file_count * sizeof(size_t));
  if (!parent) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* Initially, each file is in its own set */
  for (size_t i = 0; i < file_count; i++) {
    parent[i] = i;
  }

  /* Important to zero-initialize */
  anu_dupe_group segment_results = {0};
  for (size_t i = 0; i < file_count; i++) {

    for (size_t seg = 0; seg < config->segments; seg++) {
      segment_results.file_count = 0;

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
  report.groups = calloc(report.capacity, sizeof(dupe_group_vector));
  if (!report.groups) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* Use a temporary array of stacks/dynamic arrays to bucket the files by their
  root parent */
  anu_vector *buckets = calloc(file_count, sizeof(anu_vector));
  if (!buckets) {
    ANU_DIE("Failed to allocate memory.");
  }

  for (size_t i = 0; i < file_count; i++) {
    size_t root = find_set(i, parent);
    /* Should never happen if logic is correct */
    ASSUME(root < file_count);
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
      dupe_group_vector *temp =
          realloc(report.groups, report.capacity * sizeof(dupe_group_vector));
      if (!temp) {
        ANU_DIE("Failed to allocate memory.");
      }
      report.groups = temp;
    }

    dupe_group_vector *new_group = &report.groups[report.count];
    ++report.count;
    new_group->count = buckets[i].count;
    /* Steal the memory from the stack */
    new_group->file_ids = buckets[i].items;
  }

  free(buckets);
  free(parent);
  return report;
}
