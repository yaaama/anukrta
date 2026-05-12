#include "report.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "explore.h"
#include "stack.h"
#include "tree.h"
#include "util.h"

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
static const int UNITS_IEC_COUNT = ANU_ARRAY_SIZE(units_iec);

char *get_human_sizing_iec (u64 n_bytes, char *buf) {

  int unit_index = 0;
  /* We will use this to keep track of the fractional part */
  size_t remainder = 0;

  /* >> 10 is equivalent to dividing by 1024 */
  while ((n_bytes >= 1024) && (unit_index < (UNITS_IEC_COUNT - 1))) {
    remainder = n_bytes & 1023; /* Equivalent to: n_bytes % 1024 */
    n_bytes >>= 10;             /* Equivalent to: n_bytes / 1024 */
    ++unit_index;
  }
  MAYBE_UNUSED int c;

  if (unit_index > 0) {
    /* Calculate the 2-digit decimal part using pure integer math.
     We multiply the remainder by 100, then divide by 1024 (by shifting).
     This gives us a perfectly safe 0-99 value. */
    size_t decimals = (remainder * 100) >> 10;
    c = sprintf(buf, "%" PRIu64 ".%02zu %s", n_bytes, decimals,
                units_iec[unit_index]);
  } else {
    c = sprintf(buf, "%" PRIu64 " %s", n_bytes, units_iec[unit_index]);
  }
  assert(c > 0);
  return buf;
}

static char *get_date_from_epoch (time_t *epoch_time,
                                  size_t buf_size,
                                  char *buf) {
  struct tm timeinfo = {0};
  localtime_r(epoch_time, &timeinfo);

  MAYBE_UNUSED size_t ret =
      strftime(buf, buf_size, "%d-%m-%Y %H:%M", &timeinfo);
  assert(ret > 0);
  return buf;
}

static void print_file_hashes (uint64_t *hashes, size_t hash_count) {
  printf("\t-> Hashes: [ ");

  for (size_t i = 0; i < hash_count; i++) {
    printf("%016" PRIX64 " ", hashes[i]);
  }

  printf("]\n");
}

/**
 * @brief Elect the best file based on strategy.
 * @todo Make this accept a function pointer and write our strategies separately.
 */
static void elect_best_file (dupe_group_vector *group,
                             anu_file_q *files,
                             anukrta_config *config) {

  const best_file_strat strat = config->best_file_strategy;
  /* Exit early if no strategy or if group is just 1 file */
  if (group->count == 1 || strat == BEST_FILE_NONE) {
    return;
  }

  size_t best_index = 0;
  anu_file *best_file = &(files->items[group->file_ids[0]]);

  for (usize i = 1; i < group->count; i++) {

    bool better = false;
    anu_file *candidate = &(files->items[group->file_ids[i]]);
    switch (strat) {
      case BEST_FILE_SMALLEST:
        {
          better = (candidate->size < best_file->size);
          break;
        }
      case BEST_FILE_LARGEST:
        {
          better = (candidate->size > best_file->size);
          break;
        }

      case BEST_FILE_CTIME_OLDEST:
        {
          better = (candidate->ctime < best_file->ctime);
          break;
        }
      case BEST_FILE_CTIME_NEWEST:
        {
          better = (candidate->ctime > best_file->ctime);
          break;
        }
      case BEST_FILE_MTIME_OLDEST:
        {
          better = (candidate->mtime < best_file->mtime);
          break;
        }
      case BEST_FILE_MTIME_NEWEST:
        {
          better = (candidate->mtime > best_file->mtime);
          break;
        }
      case BEST_FILE_LONGEST:
        {
          better = (candidate->duration_us > best_file->duration_us);
          break;
        }
      case BEST_FILE_SHORTEST:
        {
          better = (candidate->duration_us < best_file->duration_us);
          break;
        }
      default:
        {
          UNREACHABLE("Strategy enum is not fully accounted.");
        }
    }

    /* Resolve tie's (e.g. if both files are the same size) */
    if (!better) {
      bool is_tied = false;
      if (strat == BEST_FILE_LARGEST || strat == BEST_FILE_SMALLEST) {
        is_tied = (candidate->size == best_file->size);
      } else if (strat == BEST_FILE_MTIME_OLDEST ||
                 strat == BEST_FILE_MTIME_NEWEST) {
        is_tied = (candidate->mtime == best_file->mtime);
      } else if (strat == BEST_FILE_CTIME_OLDEST ||
                 strat == BEST_FILE_CTIME_NEWEST) {
        is_tied = (candidate->ctime == best_file->ctime);
      } else if (strat == BEST_FILE_LONGEST || strat == BEST_FILE_SHORTEST) {
        is_tied = (candidate->duration_us == best_file->duration_us);
      }

      if (is_tied) {
        better = (strcmp(candidate->path, best_file->path) < 0);
      }
    }

    if (better) {
      best_index = i;
      best_file = candidate;
    }
  }

  /*
   * TODO 08-05-2026 09:20
   * Extract swapping logic and place elsewhere
   */
  if (best_index) {
    u64 temp = group->file_ids[0];
    group->file_ids[0] = group->file_ids[best_index];
    group->file_ids[best_index] = temp;
  }
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
  printf("\n----------------------------------------");
  printf("\nMaster file was chosen using: '%s'\n",
         BEST_FILE_STRAT_STRINGS[config->best_file_strategy]);
  printf("----------------------------------------\n");

  for (size_t i = 0; i < report->count; i++) {
    dupe_group_vector *group = &report->groups[i];

    elect_best_file(group, files, config);

    printf("\n[+] Group #%zu (%zu items):\n", i + 1, group->count);

    for (size_t j = 0; j < group->count; j++) {

      size_t file_id = group->file_ids[j];
      anu_file *file = &files->items[file_id];

      char human_sizing[32];
      get_human_sizing_iec(file->size, human_sizing);

      time_t modification_t = file->mtime;
      char time_str[64];
      get_date_from_epoch(&modification_t, sizeof(time_str), time_str);

      /* Label Best and dupes if strategy is not none */
      if (config->best_file_strategy != BEST_FILE_NONE) {
        printf("%s %s\n", (j == 0 ? "  [BEST]" : "    [DUPE]"), file->path);
      } else {
        printf("  %s\n", file->path);
      }

      printf("\t[%zu] | size: %-10s | time: %-15s | duration: %-.2fs\n",
             file_id, human_sizing, time_str,
             anu_time_microseconds_to_seconds(file->duration_us));

      if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_VERBOSE)) {
        print_file_hashes((hashes + (file_id * config->segments)),
                          config->segments);
      }
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
                                bk_node *tree) {
  size_t file_count = files->count;
  anu_report report = {0};

  if (file_count == 0 || tree == NULL) {
    return report;
  }

  report.count = 0;

  /* Union-Find to identify the groups */
  size_t *parent = calloc(file_count, sizeof(*parent));
  if (!parent) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* Initially, each file is in its own set */
  for (size_t i = 0; i < file_count; i++) {
    parent[i] = i;
  }

  /* Important to zero-initialize */
  anu_vector segment_results = {0};
  anu_vector_init(&segment_results, 32, sizeof(uint64_t));
  const size_t segments = config->segments;

  for (size_t i = 0; i < file_count; i++) {
    for (size_t seg = 0; seg < segments; seg++) {
      segment_results.count = 0;

      size_t cur_hash_idx = ((i * segments) + seg);
      uint64_t current_hash = hashes[cur_hash_idx];
      bk_tree_search(tree, current_hash, config->threshold, &segment_results);

      uint64_t *matched_files = (uint64_t *) segment_results.items;
      /* Process matches for this segment */
      for (size_t k = 0; k < segment_results.count; k++) {
        size_t match_id = matched_files[k];
        assert(match_id < file_count);
        unite_sets(i, match_id, parent);
      }
    }
  }
  anu_vector_destroy(&segment_results);  // Destroy intermediate results

  /* Convert the Union-Find result into a list of groups */

  /* Initial capacity of report */
  report.capacity = 8;
  report.groups = calloc(report.capacity, sizeof(dupe_group_vector));
  if (!report.groups) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* Use a temporary array of stacks/dynamic arrays to bucket the files by their
  root parent */
  anu_vector *buckets = calloc(file_count, sizeof(*buckets));
  if (!buckets) {
    ANU_DIE("Failed to allocate memory.");
  }

  for (size_t i = 0; i < file_count; i++) {
    size_t root = find_set(i, parent);
    /* Should never happen if logic is correct */
    ASSUME(root < file_count);
    if (!buckets[root].items) {
      anu_vector_init(&buckets[root], 2, sizeof(root));
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
