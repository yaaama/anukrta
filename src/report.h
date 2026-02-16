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
} anu_duplicate_group;

/* Represents the entire report, containing multiple groups */
typedef struct {
  anu_duplicate_group *groups;
  usize count;
  usize capacity;
} anu_report;

void anu_print_report(anu_report *report, anu_file_q *files);
anu_report anu_generate_report(anu_file_q *files, u64 *hashes,
                               anukrta_config *config, bk_tree *tree);

void anu_report_destroy(anu_report *report);
#endif  // ANU_REPORT_H
