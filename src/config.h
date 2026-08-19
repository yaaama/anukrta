#ifndef ANU_CONFIG_H
#define ANU_CONFIG_H

#include <stdint.h>

#include "defs.h"
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
  /* Reserve 2 bits for Verbosity specification (0, 1, 2, or 3) */
  RT_VERBOSITY_SHIFT = 1,        /* Bits 1 to 3 (Right To Left) are reserved for verbosity specifier*/
  RT_VERBOSITY_MASK = (3U << 1), /* 3U is binary 0011 and 3U << 1 is 00110 */

/* Helper macro to retrieve verbosity level */
#define ANU_GET_VERBOSITY(flags) (((flags) & RT_VERBOSITY_MASK) >> RT_VERBOSITY_SHIFT)
#define ANU_SET_VERBOSITY(flags, v_lvl) ((flags) |= ((v_lvl) << RT_VERBOSITY_SHIFT))

  /** Only scan current directory. */
  RT_SCAN_CURR_DIR = (1U << 4),
  /** List the files that would be hashed if run. */
  RT_DRY_RUN = (1U << 5),
  /** Store results in cache */
  RT_CACHE = (1U << 6),
} runtime_flags;

/* START: BEST_FILE_STRATEGIES */
#define BEST_FILE_STRATEGIES(X)                                 \
  X(BEST_FILE_NONE, "No Strategy")                              \
  X(BEST_FILE_LARGEST, "Largest File")                          \
  X(BEST_FILE_SMALLEST, "Smallest File")                        \
  X(BEST_FILE_CTIME_OLDEST, "Oldest Change Time (ctime)")       \
  X(BEST_FILE_CTIME_NEWEST, "Newest Change Time (ctime)")       \
  X(BEST_FILE_MTIME_OLDEST, "Oldest Modification Time (mtime)") \
  X(BEST_FILE_MTIME_NEWEST, "Newest Modification Time (mtime)") \
  X(BEST_FILE_LONGEST, "Longest Video Duration")                \
  X(BEST_FILE_SHORTEST, "Shortest Video Duration")

#define GENERATE_ENUM(ENUM_NAME, STRING_VAL) ENUM_NAME,

typedef enum best_file_strat { BEST_FILE_STRATEGIES(GENERATE_ENUM) } best_file_strat;

#define GENERATE_STRING(ENUM_NAME, STRING_VAL) [ENUM_NAME] = (STRING_VAL),

static const char *const BEST_FILE_STRAT_STRINGS[] = {BEST_FILE_STRATEGIES(GENERATE_STRING)};

#undef GENERATE_ENUM
#undef GENERATE_STRING

/* END: BEST_FILE_STRATEGIES */

typedef enum report_flags : uint32_t {
  REPORT_PRINT_HASHES = (1U << 0),
  REPORT_PRINT_UNIQUE_FILES = (1U << 1),
} report_flags;

/* Structure describing the configuration settings to use for this run. */
typedef struct anu_config {
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
  flags32 detect_flags;
  /** Bitmask flags for runtime settings.
   * @see `runtime_flags`. */
  flags32 runtime_flags;
  /** Bitmask flags determining the final report format and contents.
   * @see `report_flags`. */
  flags32 report_flags;
  /** Hashing algorithm to use.
   * @see `anu_hash_type`.*/
  anu_hash_type hash_algorithm;
  /** Strategy for determining the 'best' file out of a group of duplicates.
   * @see `best_file_strat`. */
  best_file_strat best_file_strategy;
} anu_config;

static ALWAYS_INLINE _const_ anu_config anukrta_default_config (void) {

  anu_config config = {
    .segments = 3,
    .threshold = 8,
    .hash_algorithm = ANU_HASH_ALGO_DCT,
    .skip_duration = 3,
    .thread_count = 1,
    .runtime_flags = 0,
    .detect_flags = 0,
    .report_flags = 0,
    .best_file_strategy = BEST_FILE_LONGEST,
  };

  ANU_SET_FLAG(config.detect_flags, DETECT_ROTATION);
  ANU_SET_FLAG(config.detect_flags, DETECT_BARS);
  ANU_SET_FLAG(config.detect_flags, DETECT_BLACK_FRAME);
  ANU_SET_FLAG(config.runtime_flags, RT_CACHE);
  ANU_SET_FLAG(config.report_flags, REPORT_PRINT_UNIQUE_FILES);
  return config;
}

#endif  // ANU_CONFIG_H
