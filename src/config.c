#include "config.h"

#include "util.h"

AK_CONST ak_config anukrta_default_config (void) {

  ak_config config = {
    .segments = AK_CFG_DEFAULT_SEGMENTS,
    .threshold = AK_CFG_DEFAULT_THRESHOLD,
    .hash_algorithm = AK_HASH_ALGO_DCT,
    .skip_duration = AK_CFG_DEFAULT_SKIP_DURATION,
    .thread_count = 0, /* Keep it to 0 for auto detect */
    .runtime_flags = 0,
    .detect_flags = 0,
    .report_flags = 0,
    .best_file_strategy = AK_BEST_FILE_STRAT_LARGEST, /* Largest file is considered 'best' */
    .verbosity = 0,                                   /* Silent mode */
  };

  config.detect_flags |= AK_DETECT_FLAG_ROTATION;
  config.detect_flags |= AK_DETECT_FLAG_BARS;
  config.detect_flags |= AK_DETECT_FLAG_BLACK_FRAME;
  config.runtime_flags |= AK_RUNTIME_FLAG_CACHE;
  config.runtime_flags |= AK_RUNTIME_FLAG_PROGRESS_BAR;
  config.report_flags |= AK_REPORT_FLAG_PRINT_UNIQUE_FILES;
  config.report_flags |= AK_REPORT_FLAG_HUMAN_SIZE;
  config.report_flags |= AK_REPORT_FLAG_HUMAN_TIME;
  return config;
}

AK_CONST char *ak_best_file_strat_str (AK_BEST_FILE_STRAT_ENUM strat) {

#define GENERATE_CASE_STMTS(ENUM_NAME, STRING_VAL) \
  case (ENUM_NAME):                                \
    return STRING_VAL;

  switch (strat) {
    AK__BEST_FILE_STRATEGIES_(GENERATE_CASE_STMTS);
    default:
      AK_UNREACHABLE("Strategy not found.");
  }

#undef GENERATE_CASE_STMTS
}
