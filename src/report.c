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
#include "defs.h"
#include "explore.h"
#include "kvec.h"
#include "log.h"
#include "mem.h"
#include "tree.h"
#include "util.h"

static usize find_set (usize i, usize *parent) {
  usize root = i;

  /* Pass 1: Find the actual root */
  while (parent[root] != root) {
    root = parent[root];
  }

  /* Pass 2: Path compression
   * Traverse the path again, making every node point to the root */
  while (parent[i] != root) {
    usize next_node = parent[i];
    parent[i] = root;
    i = next_node;
  }

  return root;
}

/* Merges the sets containing elements 'i' and 'j' */
static void unite_sets (usize i,
                        usize j,
                        usize *restrict parent,
                        usize *restrict rank) {
  usize root_i = find_set(i, parent);
  usize root_j = find_set(j, parent);

  if (root_i != root_j) {
    if (rank[root_i] < rank[root_j]) {
      parent[root_i] = root_j;
    } else if (rank[root_i] > rank[root_j]) {
      parent[root_j] = root_i;
    } else {
      parent[root_j] = root_i;
      rank[root_i]++;
    }
  }
}

static const char *units_iec[] = {"B", "KiB", "MiB", "GiB", "TiB"};
static const int UNITS_IEC_COUNT = ANU_ARRAY_SIZE(units_iec);

char *get_human_sizing_iec (u64 n_bytes, char *buf) {

  int unit_index = 0;
  /* We will use this to keep track of the fractional part */
  usize remainder = 0;

  /* >> 10 is equivalent to dividing by 1024 */
  while ((n_bytes >= 1024) && (unit_index < (UNITS_IEC_COUNT - 1))) {
    remainder = n_bytes & 1023; /* Equivalent to: n_bytes % 1024 */
    n_bytes >>= 10;             /* Equivalent to: n_bytes / 1024 */
    ++unit_index;
  }
  _unused_ int c;

  if (unit_index > 0) {
    /* Calculate the 2-digit decimal part using pure integer math.
     We multiply the remainder by 100, then divide by 1024 (by shifting).
     This gives us a perfectly safe 0-99 value. */
    usize decimals = (remainder * 100) >> 10;
    c = sprintf(buf, "%" PRIu64 ".%02zu %s", n_bytes, decimals,
                units_iec[unit_index]);
  } else {
    c = sprintf(buf, "%" PRIu64 " %s", n_bytes, units_iec[unit_index]);
  }
  assert(c > 0);
  return buf;
}

static char *get_date_from_epoch (time_t *epoch_time,
                                  usize buf_size,
                                  char *buf) {
  struct tm timeinfo = {0};
  localtime_r(epoch_time, &timeinfo);

  usize ret = strftime(buf, buf_size, "%d-%m-%Y %H:%M", &timeinfo);

  if (ret == 0) {
    log_warn("Date string exceeds buffer size.");
    return NULL;
  }
  return buf;
}

static void print_file_hashes (const u64 *hashes, const usize hash_count) {
  if (hash_count == 0 || hashes == NULL) {
    return;
  }
  printf("    -> Hashes: [ ");

  for (usize i = 0; i < hash_count; i++) {
    printf("%016" PRIX64 " ", hashes[i]);
  }

  printf("]");
}

static bool is_better_file (const anu_file *restrict candidate,
                            const anu_file *restrict current_best,
                            best_file_strat strat) {
  bool better = false;
  bool tied = false;

  switch (strat) {
    case BEST_FILE_SMALLEST:
      better = (candidate->size < current_best->size);
      tied = (candidate->size == current_best->size);
      break;
    case BEST_FILE_LARGEST:
      better = (candidate->size > current_best->size);
      tied = (candidate->size == current_best->size);
      break;
    case BEST_FILE_CTIME_OLDEST:
      better = (candidate->ctime < current_best->ctime);
      tied = (candidate->ctime == current_best->ctime);
      break;
    case BEST_FILE_CTIME_NEWEST:
      better = (candidate->ctime > current_best->ctime);
      tied = (candidate->ctime == current_best->ctime);
      break;
    case BEST_FILE_MTIME_OLDEST:
      better = (candidate->mtime < current_best->mtime);
      tied = (candidate->mtime == current_best->mtime);
      break;
    case BEST_FILE_MTIME_NEWEST:
      better = (candidate->mtime > current_best->mtime);
      tied = (candidate->mtime == current_best->mtime);
      break;
    case BEST_FILE_LONGEST:
      better = (candidate->duration_us > current_best->duration_us);
      tied = (candidate->duration_us == current_best->duration_us);
      break;
    case BEST_FILE_SHORTEST:
      better = (candidate->duration_us < current_best->duration_us);
      tied = (candidate->duration_us == current_best->duration_us);
      break;
    default:
      ANU_UNREACHABLE("Strategy enum is not fully accounted.");
  }

  /* Universal string tie-breaker for deterministic outputs */
  if (tied) {
    return strcmp(candidate->path, current_best->path) < 0;
  }
  return better;
}

/**
 * @brief Elect the best file based on strategy.
 * @todo Make this accept a function pointer and write our strategies separately.
 */
static void elect_best_file (u64_vec *group,
                             anu_file_vec *files,
                             anu_config *config) {

  /* Exit early if no strategy or if group is just 1 file */
  usize group_count = kv_size(*group);

  if (group_count <= 1 || config->best_file_strategy == BEST_FILE_NONE) {
    return;
  }

  const best_file_strat strat = config->best_file_strategy;

  usize best_index = 0;
  anu_file *best_file = &kv_A(*files, (kv_A(*group, 0)));
  bool better = false;

  for (usize i = 1; i < group_count; i++) {

    usize curr_file_id = kv_A(*group, i);
    anu_file *candidate = &kv_A(*files, curr_file_id);
    better = is_better_file(candidate, best_file, strat);

    if (better) {
      best_index = i;
      best_file = candidate;
    }
  }

  /* TODO: Extract swapping logic and place elsewhere */
  if (best_index) {
    u64 temp = kv_A(*group, 0);
    kv_A(*group, 0) = kv_A(*group, best_index);
    kv_A(*group, best_index) = temp;
  }
}

static void print_file_item (const anu_config *config,
                             const anu_file_vec *files,
                             const u64 *hashes,
                             usize file_id,
                             const char *tag) {

  const anu_file *file = &files->items[file_id];
  char sz[32];
  char dt[64];
  time_t t = (time_t) file->mtime;

  get_human_sizing_iec(file->size, sz);
  get_date_from_epoch(&t, sizeof(dt), dt);

  // Format: "[TAG] path" or "  path"
  if (tag) {
    printf("%s %s\n", tag, file->path);
  } else {
    printf("  %s\n", file->path);
  }

  printf("%20s | %-.2fs | %-15s\n", sz,
         anu_time_microseconds_to_seconds(file->duration_us), dt);

  if (hashes && ANU_HAS_ANY_FLAG(config->report_flags, REPORT_PRINT_HASHES)) {
    print_file_hashes(hashes + (file_id * config->segments), config->segments);
    printf("\n");
  }
}

void anu_print_report (anu_config *config,
                       anu_report *report,
                       anu_file_vec *files,
                       u64 *hashes) {
  usize group_count = kv_size(report->groups);

  if (group_count == 0) {
    printf("No duplicate groups found.\n");
    return;
  }

  printf("\n=== Duplicate Report: ===\n");

  usize file_count = kv_size(*files);
  const char *strat_str = BEST_FILE_STRAT_STRINGS[config->best_file_strategy];

  printf("Found %zu duplicate groups from %zu files\n", group_count,
         file_count);
  printf("\n+----------------------------------------------+");
  printf("\n \"Best\" file strategy: '%s'\n", strat_str);
  printf("+----------------------------------------------+\n");

  bool use_tags = (config->best_file_strategy != BEST_FILE_NONE);

  for (usize i = 0; i < group_count; i++) {
    u64_vec *group = &kv_A(report->groups, i);
    printf("\n[+] Group #%zu (%zu items):\n", i + 1, kv_size(*group));

    for (usize j = 0; j < kv_size(*group); j++) {
      const char *tag = (j == 0 && use_tags) ? "  [BEST]" : "        ";

      print_file_item(config, files, hashes, kv_A(*group, j), tag);
    }
  }

  bool print_unique =
      ANU_HAS_ANY_FLAG(config->report_flags, REPORT_PRINT_UNIQUE_FILES);
  size_t unique_count = kv_size(report->unique);
  if (print_unique) {
    printf("\nFound %zu unique files:\n", unique_count);
    for (usize i = 0; i < unique_count; i++) {
      print_file_item(config, files, hashes, kv_A(report->unique, i), NULL);
    }
  }
}

anu_report anu_generate_report (anu_file_vec *files,
                                u64 *hashes,
                                anu_config *config,
                                bk_node *tree) {

  usize file_count = kv_size(*files);
  anu_report report = {0};

  if (file_count == 0 || tree == NULL) {
    return report;
  }
  kv_init(report.unique);

  /* Union-Find to identify the groups */
  usize *parent __free(ptr) = NULL;
  parent = xmalloc(file_count * sizeof(*parent) * 2);

  usize *rank = parent + file_count;

  for (usize i = 0; i < file_count; i++) {
    /* Initially, each file is in its own set */
    parent[i] = i;
    /* Initialise ranks as 0 */
    rank[i] = 0;
  }

  u64_vec segment_results;
  kv_init(segment_results);

  const usize segment_count = config->segments;

  for (usize i = 0; i < file_count; i++) {
    for (usize seg = 0; seg < segment_count; seg++) {
      /* Reset segments_result vector to 0 */
      segment_results.size = 0;

      u64 current_hash = hashes[((i * segment_count) + seg)];
      /* Search for matches for this hash */
      bk_tree_search(tree, current_hash, config->threshold, &segment_results);

      /* Process matches for this segment */
      usize results_count = kv_size(segment_results);
      for (usize j = 0; j < results_count; j++) {
        u64 node_id = kv_A(segment_results, j);
        unite_sets(i, node_id, parent, rank);
      }
    }
  }
  /* Destroy intermediate results */
  kv_destroy(segment_results);

  /* Convert the Union-Find result into a list of groups */

  /* Use a temporary array of stacks/dynamic arrays to bucket the files by their
  root parent */
  u64_vec *buckets = xcalloc(file_count, sizeof(*buckets));

  /* Every bucket is their own parent in the beginning */
  for (u64 i = 0; i < file_count; i++) {
    usize root = find_set(i, parent);
    /* Should never happen if logic is correct */
    ANU_ASSUME(root < file_count);
    kv_push(buckets[root], (u64) i);
  }

  /* NOTE: If a bucket has more than one file, it's a duplicate group */
  /* Populate final report struct */
  for (usize i = 0; i < file_count; i++) {

    usize bucket_size = kv_size(buckets[i]);

    if (bucket_size == 1) {
      /* Unique file */
      kv_push(report.unique, kv_A(buckets[i], 0));
    }

    /* Destroy buckets with less than 1 file */
    if (bucket_size <= 1) {
      kv_destroy(buckets[i]);
    } else {
      kv_push(report.groups, buckets[i]);
    }
  }

  free(buckets);

  /* Sort file by strategy */
  for (size_t i = 0; i < report.groups.size; i++) {
    elect_best_file(&kv_A(report.groups, i), files, config);
  }

  return report;
}

void anu_report_destroy (anu_report *report) {
  /* Free all file id vectors */
  usize group_count = report->groups.size;
  for (usize i = 0; i < group_count; i++) {
    u64_vec *vec = &(kv_A(report->groups, i));
    kv_destroy(*vec);
  }
  kv_destroy(report->groups);
  kv_destroy(report->unique);
}
