#ifndef ANU_CONFIG_H
#define ANU_CONFIG_H

#include "util.h"

typedef enum anu_hash_type {
  ANU_HASH_ALGO_AVERAGE = 0,
  ANU_HASH_ALGO_DCT = 1,
} anu_hash_type;

#define ANU_REPORT_FLAG_PRINT_FILE_HASHES (1 << 0)
#define ANU_DETECTION_FLAG_BAR (1 << 0)
#define ANU_DETECTION_FLAG_BLACK_FRAME (1 << 1)
#define ANU_DETECTION_ROTATION (1 << 2)

/* Structure describing the configuration settings to use for this run. */
typedef struct anukrta_config {
  char **paths;
  size_t skip_duration; /* Video length shorter than this will be skipped */
  size_t paths_count;   /* Number of paths we parsed from cli args */
  int threshold;        /* Similarity threshold */
  int segments;         /* Number of segments to hash from a video */
  size_t thread_count;  /* Number of threads */
  b32 verbose;          /* Turn on verbose output */
  b32 scan_curr_dir;    /* Only scan current directory */
  b32 _exit_early;      /* Exit quickly (set when we parse '-h' for example) */
  b32 dry_run;          /* TODO Do not save/actually process any files */
  b32 detect_black_frames; /* TODO Detect black frames in video and skip them? */
  b32 detect_rotation;     /* TODO Detect rotation in videos? */
  b32 detect_bars;         /* Detect windowboxing/letterboxing/pillarboxing */
  anu_hash_type hash_algorithm; /* Hashing algorithm to use */
  int report_flags;
} anukrta_config;

#endif  // ANU_CONFIG_H
