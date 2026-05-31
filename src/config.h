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
  /** Store results in cache */
  RT_CACHE = (1U << 4),
} runtime_flags;

/* START: BEST_FILE_STRATEGIES */
#define BEST_FILE_STRATEGIES(X)                                 \
  X(BEST_FILE_NONE, "No strategy")                              \
  X(BEST_FILE_LARGEST, "Largest file")                          \
  X(BEST_FILE_SMALLEST, "Smallest file")                        \
  X(BEST_FILE_CTIME_OLDEST, "Oldest change time (ctime)")       \
  X(BEST_FILE_CTIME_NEWEST, "Newest change time (ctime)")       \
  X(BEST_FILE_MTIME_OLDEST, "Oldest modification time (mtime)") \
  X(BEST_FILE_MTIME_NEWEST, "Newest modification time (mtime)") \
  X(BEST_FILE_LONGEST, "Longest video duration")                \
  X(BEST_FILE_SHORTEST, "Shortest video duration")

#define GENERATE_ENUM(ENUM_NAME, STRING_VAL) ENUM_NAME,

typedef enum best_file_strat {
  BEST_FILE_STRATEGIES(GENERATE_ENUM)
} best_file_strat;

#undef GENERATE_ENUM

#define GENERATE_STRING(ENUM_NAME, STRING_VAL) [ENUM_NAME] = (STRING_VAL),

static const char *const BEST_FILE_STRAT_STRINGS[] = {
  BEST_FILE_STRATEGIES(GENERATE_STRING)};

#undef GENERATE_STRING

/* END: BEST_FILE_STRATEGIES */

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
  /** Bitmask flags for similarity detection.
   * @see `detection_flags`. */
  bflag32 detect_flags;
  /** Bitmask flags for runtime settings.
   * @see `runtime_flags`. */
  bflag32 runtime_flags;
  /** Bitmask flags determining the final report format and contents.
   * @see `report_flags`. */
  bflag32 report_flags;
  /** Hashing algorithm to use.
   * @see `anu_hash_type`.*/
  anu_hash_type hash_algorithm;
  /** Strategy for determining the 'best' file out of a group of duplicates.
   * @see `best_file_strat`. */
  best_file_strat best_file_strategy;
} anukrta_config;

#endif  // ANU_CONFIG_H
