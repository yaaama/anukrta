#ifndef ANU_REPORT_H
#define ANU_REPORT_H

#include "config.h"
#include "explore.h"
#include "tree.h"
#include "util.h"

/* A dynamic array of file IDs */
typedef struct {
  u64 *file_ids;
  usize count;
  usize capacity;
} dupe_group_vector;

typedef uint64_t file_id;

/* Vector of file ids */
typedef kvec_t(uint64_t) file_id_vec;

/* List of a list of file ids */
/* [0 : [id1,id2], 1: [id2,id3]] */
typedef kvec_t(file_id_vec) group_vector;

/*
 * Represents the entire report, containing multiple groups.
 * This will be printed after the program has hashed all files.
 */
typedef struct {
  group_vector groups;
} anu_report;

void anu_print_report(anukrta_config *config,
                      anu_report *report,
                      anu_file_q *files,
                      u64 *hashes);
anu_report anu_generate_report(anu_file_q *files,
                               u64 *hashes,
                               anukrta_config *config,
                               bk_node *tree);

void anu_report_destroy(anu_report *report);

char *get_human_sizing_iec(u64 n_bytes, char *buf);
#endif  // ANU_REPORT_H
