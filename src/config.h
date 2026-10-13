#ifndef AK_CONFIG_H
#define AK_CONFIG_H

#include <stdint.h>

#include "defs.h"
#include "util.h"

#define AK_VERSION 0.0.1
#define AK_VERSION_STR AK_STRINGIFY(AK_VERSION)

typedef enum AK_HASH_TYPE_ENUM {
  AK_HASH_ALGO_AVERAGE = 0, /**< TODO Implement an average hash. */
  AK_HASH_ALGO_DCT,         /**< Standard DCT hashing. */
} AK_HASH_TYPE_ENUM;

typedef enum AK_DETECT_FLAGS_ENUM : uint32_t {
  /** Detect black frames and skip them. */
  AK_DETECT_FLAG_BLACK_FRAME = (1U << 0),
  /** Detect window/pillar/letter boxing and discard those pixels. */
  AK_DETECT_FLAG_BARS = (1U << 1),
  /** Detect rotation of frame. */
  AK_DETECT_FLAG_ROTATION = (1U << 2),
} AK_DETECT_FLAGS_ENUM;

typedef enum AK_RUNTIME_FLAGS_ENUM : uint32_t {
  /** @private Internal flag to exit quickly (set when parsing '-h', etc). */
  AK_RUNTIME_FLAG_EXIT_EARLY = (1U << 0),
  /** Only scan current directory. */
  AK_RUNTIME_FLAG_SCAN_CURR_DIR = (1U << 1),
  /** List the files that would be hashed if run. */
  AK_RUNTIME_FLAG_DRYRUN = (1U << 2),
  /** Read from and write to the cache */
  AK_RUNTIME_FLAG_CACHE = (1U << 3),
  AK_RUNTIME_FLAG_PROGRESS_BAR = (1U << 4),
} AK_RUNTIME_FLAGS_ENUM;

typedef enum AK_REPORT_FLAGS_ENUM : uint32_t {
  AK_REPORT_FLAG_PRINT_HASHES = (1U << 0),
  AK_REPORT_FLAG_PRINT_UNIQUE_FILES = (1U << 1),
  AK_REPORT_FLAG_FMT_JSON = (1U << 2),
  AK_REPORT_FLAG_HUMAN_SIZE = (1U << 3),
  AK_REPORT_FLAG_HUMAN_TIME = (1U << 4),
} AK_REPORT_FLAGS_ENUM;

/* START: BEST_FILE_STRATEGIES */
#define AK__BEST_FILE_STRATEGIES_(X)                                     \
  X(AK_BEST_FILE_STRAT_NONE, "No Strategy")                              \
  X(AK_BEST_FILE_STRAT_LARGEST, "Largest File")                          \
  X(AK_BEST_FILE_STRAT_SMALLEST, "Smallest File")                        \
  X(AK_BEST_FILE_STRAT_CTIME_OLDEST, "Oldest Change Time (ctime)")       \
  X(AK_BEST_FILE_STRAT_CTIME_NEWEST, "Newest Change Time (ctime)")       \
  X(AK_BEST_FILE_STRAT_MTIME_OLDEST, "Oldest Modification Time (mtime)") \
  X(AK_BEST_FILE_STRAT_MTIME_NEWEST, "Newest Modification Time (mtime)") \
  X(AK_BEST_FILE_STRAT_LONGEST, "Longest Video Duration")                \
  X(AK_BEST_FILE_STRAT_SHORTEST, "Shortest Video Duration")

#define GENERATE_ENUM(ENUM_NAME, STRING_VAL) ENUM_NAME,

typedef enum AK_BEST_FILE_STRAT_ENUM {
  AK__BEST_FILE_STRATEGIES_(GENERATE_ENUM)
} AK_BEST_FILE_STRAT_ENUM;

#undef GENERATE_ENUM

#define GENERATE_STRING(ENUM_NAME, STRING_VAL) [ENUM_NAME] = (STRING_VAL),
static const char *const AK_BEST_FILE_STRAT_STRV[] = {AK__BEST_FILE_STRATEGIES_(GENERATE_STRING)};
#undef GENERATE_STRING

char *ak_best_file_strat_str(AK_BEST_FILE_STRAT_ENUM strat);

/* END: AK__BEST_FILE_STRATEGIES_ */

/* Structure describing the configuration settings to use for this run. */
typedef struct anu_config {
  /** Video length shorter than this will be skipped (seconds) */
  usize skip_duration;
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
   * @see `AK_RUNTIME_FLAGS_ENUM`. */
  flags32 runtime_flags;
  /** Bitmask flags determining the final report format and contents.
   * @see `AK_REPORT_FLAGS_ENUM`. */
  flags32 report_flags;
  /** Hashing algorithm to use.
   * @see `AK_HASH_TYPE_ENUM`.*/
  AK_HASH_TYPE_ENUM hash_algorithm;
  /** Strategy for determining the 'best' file out of a group of duplicates.
   * @see `AK_BEST_FILE_STRAT_ENUM`. */
  AK_BEST_FILE_STRAT_ENUM best_file_strategy;
  /**
   * Verbosity ranging from 0 to 3
   * level 0: No logging output (default).
   * level 1: Errors/informational logging.
   * level 2: Warnings + Debugging logging.
   * level 3: Trace logging.
   */
  u8 verbosity;
  byte padding[3];
} ak_config;

#define AK_CFG_DEFAULT_SEGMENTS 3
#define AK_CFG_DEFAULT_THRESHOLD 8
#define AK_CFG_DEFAULT_SKIP_DURATION 3

/**
 * Return a default configuration object.
 */
AK_CONST ak_config anukrta_default_config(void);

#endif  // AK_CONFIG_H
