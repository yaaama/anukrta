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

typedef struct {
  u64 file_id;
  u64 root_id;
} uf_pair;

static usize find_set (usize i, usize *parent) {
  usize root = i;
  while (parent[root] != root) {
    parent[root] = parent[parent[root]];
    root = parent[root];
  }
  return root;
}

/* Merges the sets containing elements 'i' and 'j' */
static void unite_sets (usize i, usize j, usize *restrict parent, usize *restrict rank) {
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
static const int UNITS_IEC_COUNT = AK_ARRAY_SIZE(units_iec);

char *get_human_sizing_iec (u64 n_bytes, char *buf, usize buf_size) {

  double bytes = (double) n_bytes;
  int unit_index = 0;

  /* >> 10 is equivalent to dividing by 1024 */
  while ((bytes >= 1024.0) && (unit_index < (UNITS_IEC_COUNT - 1))) {
    bytes /= 1024.0; /* Equivalent to: bytes / 1024 */
    ++unit_index;
  }
  AK_UNUSED int c;

  if (unit_index == 0) {
    c = snprintf(buf, buf_size, "%.0f %s", bytes, units_iec[unit_index]);
  } else {
    c = snprintf(buf, buf_size, "%.2f %s", bytes, units_iec[unit_index]);
  }

  /* If buffer was not large enough to hold the formatted string,
   * we clear the buffer of the printed characters and then return null */
  if ((size_t) c >= buf_size) {
    ak_memzero_sz(buf, buf_size);
    return NULL;
  };
  return buf;
}

static char *get_date_from_epoch (time_t *epoch_time, char *buf, usize buf_size) {
  struct tm timeinfo = {0};
  localtime_r(epoch_time, &timeinfo);

  usize ret = strftime(buf, buf_size, "%d-%m-%Y %H:%M", &timeinfo);

  if (ret == 0) {
    log_warn("Date string exceeds buffer size.");
    snprintf(buf, buf_size, "UNKNOWN");
  }

  return buf;
}

static void print_file_hashes (const ak_hash_entry *entries, const usize entries_count) {
  if (entries_count == 0 || entries == NULL) {
    return;
  }
  printf("    -> Hashes: [ ");

  for (usize i = 0; i < entries_count; i++) {
    printf("%016" PRIX64 " ", entries[i].hash);
  }

  printf("]");
}

static bool is_better_file (const ak_file *restrict candidate,
                            const ak_file *restrict current_best,
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
      AK_UNREACHABLE("Strategy enum is not fully accounted.");
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
static void elect_best_file (u64_vec *group, ak_file_v *files, ak_config *config) {

  /* Exit early if no strategy or if group is just 1 file */
  usize group_count = kv_size(*group);

  if (group_count <= 1 || config->best_file_strategy == BEST_FILE_NONE) {
    return;
  }

  const best_file_strat strat = config->best_file_strategy;

  usize best_index = 0;
  u64 idx = kv_A(*group, 0);
  ak_file *best_file = &kv_A(*files, idx);
  bool better = false;

  for (usize i = 1; i < group_count; i++) {

    usize curr_file_id = kv_A(*group, i);
    ak_file *candidate = &kv_A(*files, curr_file_id);
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

static void print_file_item (const ak_config *config,
                             const ak_file_v *files,
                             const AK_STATUS AK_UNUSED result,
                             const ak_hash_entry *entries,
                             usize file_id,
                             const char *tag) {

  const ak_file *file = &files->items[file_id];
  char sz[32];
  char dt[64];
  time_t t = (time_t) file->mtime;

  bool valid_size = get_human_sizing_iec(file->size, sz, AK_ARRAY_SIZE(sz)) != NULL;

  if (ak_unlikely(!valid_size)) {
    AK_PANIC("Buffer sizing is too small! Update it to be bigger.");
  }

  get_date_from_epoch(&t, dt, AK_ARRAY_SIZE(dt));

  // Format: "[TAG] path" or "  path"
  if (tag) {
    printf("%s %s\n", tag, file->path);
  } else {
    printf("  %s\n", file->path);
  }

  printf("%20s | %-.2fs | %-15s\n", sz, ak_time_microsec_sec(file->duration_us), dt);
  /* entry may be NULL if we have not hashed the file (because it was skipped for example) */
  int print_hashes = (entries && ak_flag_has(config->report_flags, REPORT_PRINT_HASHES));
  if (print_hashes) {
    print_file_hashes(entries + (file_id * config->segments), config->segments);
    printf("\n");
  }
}

static const char *get_skip_reason_string (AK_STATUS status) {
  switch (status) {
    case AK_SKIP_SHORT_DURATION:
      return "VIDEO TOO SHORT";
    case AK_IO_FAIL:
      return "I/O FAILURE";
    default:
      return "UNKNOWN";
  }
}

void ak_report_print (ak_config *config,
                      ak_report *report,
                      ak_file_v *files,
                      AK_STATUS *results,
                      ak_hash_entry *entries) {

  usize group_count = kv_size(report->groups);

  if (group_count == 0) {
    printf("No duplicate groups found.\n");
    return;
  }

  printf("\n=== Duplicate Report: ===\n");

  usize file_count = kv_size(*files);
  size_t unique_count = kv_size(report->unique);
  size_t skipped_count = kv_size(report->skipped);
  const char *strat_str = BEST_FILE_STRAT_STRINGS[config->best_file_strategy];

  printf("Found %zu duplicate groups from %zu files\n", group_count, file_count);
  printf("\n+----------------------------------------------+");
  printf("\n \"Best\" file strategy: '%s'\n", strat_str);
  printf("+----------------------------------------------+\n");

  bool use_tags = (config->best_file_strategy != BEST_FILE_NONE);
  bool print_unique = ak_flag_has(config->report_flags, REPORT_PRINT_UNIQUE_FILES);

  for (usize i = 0; i < group_count; i++) {
    u64_vec *group = &kv_A(report->groups, i);
    printf("\n[+] Group #%zu (%zu items):\n", i + 1, kv_size(*group));

    for (usize j = 0; j < kv_size(*group); j++) {
      const char *tag = (j == 0 && use_tags) ? "  [BEST]" : "        ";
      usize file_id = kv_A(*group, j);
      print_file_item(config, files, results[file_id], entries, file_id, tag);
    }
  }

  if (print_unique) {
    printf("\nFound %zu unique files:\n", unique_count);
    for (usize i = 0; i < unique_count; i++) {
      usize file_id = kv_A(report->unique, i);
      print_file_item(config, files, results[file_id], entries, file_id, NULL);
    }
  }

  printf("\nSkipped %zu files:\n", skipped_count);
  for (usize i = 0; i < skipped_count; i++) {
    usize file_id = kv_A(report->skipped, i);
    /* const anu_file *file = &files->items[file_id]; */
    i32 status = results[file_id];
    print_file_item(config, files, status, NULL, file_id, "  ");
    printf("        -> Reason: %s\n", get_skip_reason_string(status));
  }
}

static int compare_uf_pairs (const void *a, const void *b) {
  const uf_pair *pa = (const uf_pair *) a;
  const uf_pair *pb = (const uf_pair *) b;

  if (pa->root_id < pb->root_id) {
    return -1;
  }
  if (pa->root_id > pb->root_id) {
    return 1;
  }

  /* If it is a tie, then sort by file_id for deterministic output */
  if (pa->file_id < pb->file_id) {
    return -1;
  }
  if (pa->file_id > pb->file_id) {
    return 1;
  }

  return 0;
}

ak_report ak_report_build (ak_file_v *files,
                           AK_STATUS *results,
                           ak_hash_entry *entries,
                           ak_config *config,
                           bk_node *tree) {

  usize file_count = kv_size(*files);
  ak_report report = {0};

  if (file_count == 0 || tree == NULL) {
    return report;
  }
  kv_init(report.unique);
  kv_init(report.skipped);

  /* Union-Find to identify the groups */
  usize *parent AK_AUTO(free) = NULL;
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
    /* File was SKIPPED */
    if ((results[i] != AK_OK) && (results[i] != AK_STATUS_FILE_CACHED)) {
      kv_push(report.skipped, (u64) i);
      continue;
    }
    for (usize seg = 0; seg < segment_count; seg++) {
      /* Reset segments_result vector to 0 */
      segment_results.size = 0;

      u64 current_hash = entries[((i * segment_count) + seg)].hash;
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

  uf_pair *pairs AK_AUTO(free) = xmalloc(file_count * sizeof(uf_pair));
  usize valid_count = 0;

  /* Every bucket is their own parent in the beginning */
  for (u64 i = 0; i < file_count; i++) {

    if (results[i] != AK_OK && results[i] != AK_STATUS_FILE_CACHED) {
      continue;
    }

    usize root = find_set(i, parent);
    /* Should never happen if logic is correct */
    AK_ASSUME(root < file_count);

    pairs[valid_count].file_id = i;
    pairs[valid_count].root_id = (u64) root;
    ++valid_count;
  }

  /* Sort the array by root_id, meaning all identical roots will be next to one another */
  if (valid_count > 0) {
    qsort(pairs, valid_count, sizeof(uf_pair), compare_uf_pairs);
  }

  /* Go through the pairs array, grouping duplicate groups and unique files into their own vectors */
  usize current_idx = 0;
  while (current_idx < valid_count) {
    u64 current_root = pairs[current_idx].root_id;
    usize group_start = current_idx;

    /* Advance current_idx until the root_id changes letting us calculate the bounds of the group */
    while ((current_idx < valid_count) && (pairs[current_idx].root_id == current_root)) {
      current_idx++;
    }

    usize group_size = current_idx - group_start;

    /* Unique file */
    if (group_size == 1) {
      kv_push(report.unique, pairs[group_start].file_id);
    }
    /* Valid group with multiple duplicate files */
    else if (group_size > 1) {
      u64_vec group;
      kv_init(group);
      /* Preallocate vector since we know the group size already */
      kv_ensure_space(group, group_size);

      /* Populate the group vector with the file ids */
      for (usize j = group_start; j < current_idx; j++) {
        kv_push_c(group, pairs[j].file_id);
      }

      /* Sort file group by user's strategy */
      elect_best_file(&group, files, config);
      kv_push(report.groups, group);
    }
  }
  return report;
}

void ak_report_destroy (ak_report *report) {
  /* Free all file id vectors */
  usize group_count = report->groups.size;
  for (usize i = 0; i < group_count; i++) {
    u64_vec *vec = &(kv_A(report->groups, i));
    kv_destroy(*vec);
  }
  kv_destroy(report->groups);
  kv_destroy(report->unique);
  kv_destroy(report->skipped);
}
