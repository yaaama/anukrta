#ifndef ANU_REPORT_H
#define ANU_REPORT_H

#include "explore.h"
#include "tree.h"
#include "util.h"

/* A dynamic array of file IDs */
typedef struct {
  u64 *file_ids;
  usize count;
  usize capacity;
} dupe_group_vector;

/*
 * Represents the entire report, containing multiple groups.
 * This will be printed after the program has hashed all files.
 */
typedef struct {
  dupe_group_vector *groups;
  usize count;
  usize capacity;
} anu_report;

void anu_print_report(anukrta_config *config,
                      anu_report *report,
                      anu_file_q *files,
                      u64 *hashes);
anu_report anu_generate_report(anu_file_q *files,
                               u64 *hashes,
                               anukrta_config *config,
                               bk_tree *tree);

void anu_report_destroy(anu_report *report);

char *get_human_sizing_iec(u64 n_bytes, size_t buf_size, char *buf);
#endif  // ANU_REPORT_H
