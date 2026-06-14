#ifndef ANU_REPORT_H
#define ANU_REPORT_H

#include "config.h"
#include "explore.h"
#include "tree.h"
#include "util.h"

/* List of a list of file ids */
/* [0 : [id1,id2], 1: [id2,id3]] */
typedef kvec_t(u64_vec) anu_report_groups;

/*
 * Represents the entire report, containing multiple groups.
 * This will be printed after the program has hashed all files.
 */
typedef struct {
  anu_report_groups groups;
} anu_report;

void anu_print_report(anukrta_config *config,
                      anu_report *report,
                      anu_file_vec *files,
                      u64 *hashes);
anu_report anu_generate_report(anu_file_vec *files,
                               u64 *hashes,
                               anukrta_config *config,
                               bk_node *tree);

void anu_report_destroy(anu_report *report);

char *get_human_sizing_iec(u64 n_bytes, char *buf);
#endif  // ANU_REPORT_H
