#ifndef ANU_CONFIG_H
#define ANU_CONFIG_H

#include "util.h"

typedef enum anu_hash_type {
  ANU_HASH_ALGO_AVERAGE = 0,
  ANU_HASH_ALGO_DCT = 1,
} anu_hash_type;

typedef enum detect_flags : uint32_t {
  /** Detect black frames and skip them. */
  DETECT_BLACK_FRAME = (1U << 0),
  /** Detect window/pillar/letter boxing and discard those pixels. */
  DETECT_BARS = (1U << 1),
  /** Detect rotation of frame. */
  DETECT_ROTATION = (1U << 2),
} detect_flags;

typedef enum runtime_flags : uint32_t {
  /** @private Internal flag to exit quickly (set when parsing '-h', etc). */
  RT_EXIT_EARLY = (1U << 0),
  /** Turn on verbose output. */
  RT_VERBOSE = (1U << 1),
  /** Only scan current directory. */
  RT_SCAN_CURR_DIR = (1U << 2),
  /** List the files that would be hashed if run. */
  RT_DRY_RUN = (1U << 3),

} runtime_flags;

typedef enum report_flags : uint32_t {
  REPORT_PRINT_HASHES = (1U << 0),
} report_flags;

/* Structure describing the configuration settings to use for this run. */
typedef struct anukrta_config {
  /** Array of file/directory paths to process. */
  char **paths;
  /** Video length shorter than this will be skipped (seconds) */
  usize skip_duration;
  /** Number of paths we parsed from cli args. */
  usize paths_count;
  /** Similarity threshold (0 to 64) with 64 being completely different. */
  usize threshold;
  /** Number of segments to hash from a video. */
  usize segments;
  /** Number of concurrent threads to spawn. */
  usize thread_count;
  /** Bitmask flags for similarity detection @see `detection_flags`*/
  bflag32 detect_flags;
  /** Bitmask flags for runtime settings @see runtime_flags */
  bflag32 runtime_flags;
  /** Bitmask flags determining the final report format and contents. */
  bflag32 report_flags;
  /** Hashing algorithm to use. */
  anu_hash_type hash_algorithm;
} anukrta_config;

#endif  // ANU_CONFIG_H
