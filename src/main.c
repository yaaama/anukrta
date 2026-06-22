/* Video similarity tool */

#ifdef ANU_DEBUG
#  pragma message "Compilation in DEBUG mode."
#  ifdef NDEBUG
#    undef NDEBUG
#  endif
#endif

#include <assert.h>
#include <inttypes.h>
#include <libavutil/log.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "cli.h"
#include "config.h"
#include "defs.h"
#include "explore.h"
#include "kvec.h"
#include "log.h"
#include "mem.h"
#include "report.h"
#include "sqlite3.h"
#include "tree.h"
#include "util.h"
#include "video.h"

/**
 * @brief Struct returned by threads.
 */
typedef struct {
  alignas(CACHE_LINE_SIZE) ANU_STATUS value; /**< Result code. */
} worker_result;

/**
 * @brief Context data passed to threads.
 */
typedef struct {
  /** Pointer to file queue that needs to be hashed. */
  anu_file_vec *files;

  /** Pointer to program configuration. */
  anukrta_config *config;

  /** Array of hashes that are produced during thread execution. */
  uint64_t *hashes;

  /** Array of timestamps for each hash. */
  uint64_t *frame_timestamps;

  /** Array of result codes from threads. */
  worker_result *results;

  /** Number of files to prodcess. */
  size_t file_count;

  /** Index of file to process by thread worker. */
  atomic_size_t *current_idx; /* Shared index */
} worker_args;

/**
 * @brief Callback function for logger.
 */
static void log_lock_callback (bool lock, void *udata) {
  pthread_mutex_t *mutex = (pthread_mutex_t *) udata;
  if (lock) {
    pthread_mutex_lock(mutex);
  } else {
    pthread_mutex_unlock(mutex);
  }
}

static void init_logger (int verbosity,
                         pthread_mutex_t *log_mutex,
                         void (*lock_cb)(bool, void *)) {
  log_set_level(verbosity);
  log_set_lock(lock_cb, log_mutex);
}

static void *hash_worker_thread (void *arg) {
  worker_args *targs = (worker_args *) arg;

  const size_t file_count = targs->file_count;
  const size_t segments = targs->config->segments;

  while (1) {
    /* Get index of next file */
    size_t idx = atomic_fetch_add(targs->current_idx, 1);

    /* Check if next index exceeds file count */
    if (idx >= file_count) {
      break;
    }

    if (targs->results[idx].value == ANU_FILE_CACHED) {
      continue;
    }

    size_t hash_off = idx * segments;

    /* Do the hashing and store return code */
    targs->results[idx].value = anu_video_hash(
        &targs->files->items[idx], targs->config, targs->hashes + hash_off,
        targs->frame_timestamps + hash_off);
  }

  return NULL;
}

static void execute_hash_worker_threads (anukrta_config *config,
                                         worker_args *args,
                                         size_t file_count) {
  /* NOTE: Thread count should not exceed file count */
  config->thread_count = MINIMUM(config->thread_count, file_count);
  assert(config->thread_count > 0);

  pthread_t *threads __free(ptr) =
      xcalloc(config->thread_count, sizeof(*threads));
  if (!threads) {
    ANU_DIE("Failed to allocate memory for threads");
  }

  log_info("Starting %zu hashing threads...", config->thread_count);

  /* Create the threads */
  int threads_made = 0;
  for (size_t i = 0; i < config->thread_count; i++) {
    int success =
        (pthread_create(&threads[i], NULL, hash_worker_thread, args) == 0);
    threads_made += success;
    if (!success) {
      log_warn("Failed to create thread #%zu", i);
      break;
    }
  }

  /* Wait for all threads to finish */
  for (int i = 0; i < threads_made; i++) {
    pthread_join(threads[i], NULL);
  }
}

static int anukrta_driver (anukrta_config *config) {

  assert(config->segments > 0);

  /* Initialise file queue */
  anu_file_vec files __free(anu_file_vec) = {0};
  kv_init(files);

  /* Scan path(s) and store in files queue */
  anu_explore_scan_directories(config, &files);

  /* Exit early if we do not find any files */
  if (kv_size(files) < 1) {
    log_warn("No video files found!");
    return -1;
  }

  /* Number of files to hash */
  const usize file_count = kv_size(files);
  log_info("Found `%zu` files", file_count);

  /*
   * The total number of hashes produced = number of files * number of segments
   */
  const usize hash_collection_len = (file_count * config->segments);

  /* Array of hashes
   * E.g. (N files with 2 segments) would look like this:
   * [ File1Seg1, File1Seg2, File2Seg1, File2Seg2, ... File N Seg 2 ]
   * FileNSegN would be the hash created for that segment
   */
  uint64_t *hashes __free(ptr) = NULL;
  uint64_t *timestamps __free(ptr) = NULL;
  worker_result *thread_results __free(ptr) = NULL;

  hashes = xmalloc(hash_collection_len * sizeof(*hashes));
  timestamps = xmalloc(hash_collection_len * sizeof(*timestamps));
  thread_results =
      aligned_alloc(CACHE_LINE_SIZE, file_count * sizeof(*thread_results));

  if (!thread_results || !hashes || !timestamps) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* Initialise thread result values */
  for (size_t i = 0; i < file_count; i++) {
    thread_results[i].value = ANU_FILE_PENDING;
  }

  atomic_size_t current_file_idx = 0;

  /* Package the arguments */
  worker_args thread_ctx = {
    .files = &files,
    .config = config,
    .hashes = hashes,
    .frame_timestamps = timestamps,
    .results = thread_results,
    .file_count = file_count,
    .current_idx = &current_file_idx,
  };

  /* Initialise SQLite3 */
  cache_init_once();
  /* Open the database */
  anu_cache_ctx *db __free(cache_ctx) = cache_open_db("cache.db");

  if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_CACHE)) {
    log_info("Checking database cache for already hashed files...");

    for (size_t i = 0; i < file_count; i++) {
      anu_file *file = &kv_A(files, i);
      uint64_t row_id = 0;
      uint64_t file_duration_us = 0;

      /* Check if metadata is exactly the same */
      if (cache_is_file_valid(db, file, &row_id, &file_duration_us)) {
        size_t out_count = 0;
        size_t hash_off = i * config->segments;

        /* Retrieve the hashes & timestamps directly into our arrays */
        int ret =
            cache_get_hashes(db, row_id, config->segments, hashes + hash_off,
                             timestamps + hash_off, &out_count);

        /* Only use cache if the database contains the EXACT amount of segments we requested */
        if (ret == 0 && out_count == config->segments) {
          thread_results[i].value = ANU_FILE_CACHED;

          file->duration_us = (size_t) file_duration_us;
        }
      }
    }
  }

  time_t curr_time = time(NULL);
  log_debug("Current time: %ld", curr_time);

  /* THREADING START */
  execute_hash_worker_threads(config, &thread_ctx, file_count);
  /* THREADING END */

  bk_node *filetree = NULL;

  /* Begin transaction for upsertion */
  cache_begin_transaction(db);

  for (size_t i = 0; i < file_count; i++) {
    /* Current file */
    anu_file *file = &kv_A(files, i);
    ANU_STATUS result = thread_results[i].value;
    size_t file_idx = (i * config->segments);
    /* Check the result saved by the thread */

    /* Failed to hash a file */
    if (result == ANU_IO_FAIL) {
      log_debug("Failed to hash file '%s'", anu_file_get_filename(file));
    }

    /* We skipped this file */
    if (result == ANU_FILE_SKIPPED) {
      log_debug("Skipped file '%s'", anu_file_get_filename(file));
    }

    if (result == ANU_FILE_CACHED) {
      log_info("File %s previously hashed.", anu_file_get_filename(file));
      for (size_t segment_off = 0; segment_off < config->segments;
           segment_off++) {
        size_t curr_seg_idx = file_idx + segment_off;
        u64 curr_hash = hashes[curr_seg_idx];
        bk_tree_insert(&filetree, curr_hash, i);
      }
      continue;
    }

    if (result == ANU_OK) {
      /* Row ID for inserted row */
      u64 row_id = 0;
      cache_upsert_file(db, "v", file, (u64) curr_time, &row_id);

      for (size_t segment_off = 0; segment_off < config->segments;
           segment_off++) {
        size_t curr_seg_idx = file_idx + segment_off;
        u64 curr_hash = hashes[curr_seg_idx];
        u64 curr_frame = timestamps[curr_seg_idx];
        cache_insert_hash(db, row_id, curr_hash, curr_frame);
        bk_tree_insert(&filetree, curr_hash, i);
      }
    }
  }

  /* Commit the transactions */
  cache_commit_transaction(db);

  anu_report report = anu_generate_report(&files, hashes, config, filetree);
  anu_print_report(config, &report, &files, hashes);

  /* CLEANUP */
  anu_report_destroy(&report);
  bk_tree_node_free(filetree);
  /* Close the database */
  sqlite3_shutdown();

  return 0;
}

static inline anukrta_config default_config (void) {

  anukrta_config config = {
    .segments = 3,
    .threshold = 15,
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
  return config;
}

int main (int argc, char *argv[]) {

  /* Default configuration */
  anukrta_config config = default_config();

  /* Option parsing return value */
  int parsing_return = anu_cli_parse_options(&config, argc, argv);

  /* Exit if non zero or if config has exit_early flag set */
  if (parsing_return ||
      (ANU_HAS_ANY_FLAG(config.runtime_flags, RT_EXIT_EARLY))) {
    return parsing_return;
  }

  /* Logging Setup */
  pthread_mutex_t log_mutex;
  pthread_mutex_init(&log_mutex, NULL);

  int log_lvl = 0;
  int libav_log_lvl = 0;
  if (ANU_HAS_ANY_FLAG(config.runtime_flags, RT_VERBOSE)) {
    anu_cli_print_configuration(&config);
    log_lvl = LOG_DEBUG;
    libav_log_lvl = AV_LOG_INFO;
  } else {
    log_lvl = LOG_ERROR;
    libav_log_lvl = AV_LOG_FATAL;
  }
  av_log_set_level(libav_log_lvl);
  init_logger(log_lvl, &log_mutex, log_lock_callback);

  /* Start of program */
  printf("\n\n--------------------\n");

  log_info("%s now running...", argv[0]);

  int driver_ret = anukrta_driver(&config);
  pthread_mutex_destroy(&log_mutex);
  return driver_ret;
}
