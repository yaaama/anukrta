#ifndef AK_REPORT_H
#define AK_REPORT_H

#include <stddef.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "kvec.h"
#include "tree.h"

/* List of a list of file ids */
/* [0 : [id1,id2], 1: [id2,id3]] */
typedef kvec_t(u64_vec) ak_report_groups;

/*
 * Represents the entire report, containing multiple groups.
 * This will be printed after the program has hashed all files.
 */
typedef struct ak_report {
  ak_report_groups groups;
  u64_vec unique;
  u64_vec skipped;
} ak_report;

void ak_report_print(ak_config *config,
                     ak_report *report,
                     ak_file_v *files,
                     AK_STATUS *results,
                     ak_hash_entry *entries);

ak_report ak_report_build(ak_file_v *files,
                          AK_STATUS *results,
                          ak_hash_entry *entries,
                          ak_config *config,
                          bk_node *tree);

void ak_report_destroy(ak_report *report);

char *get_human_sizing_iec(u64 n_bytes, char *buf, usize buf_size);
#endif  // AK_REPORT_H
