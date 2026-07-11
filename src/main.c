/* Video similarity tool */

#ifdef ANU_DEBUG
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
#include <time.h>

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
 * Callback function for logger.
 */
static void log_lock_callback (bool lock, void *udata) {
  pthread_mutex_t *mutex = (pthread_mutex_t *) udata;
  if (lock) {
    pthread_mutex_lock(mutex);
  } else {
    pthread_mutex_unlock(mutex);
  }
}

/**
 * Setup loggers for internal logging and libav.
 *
 * @param anu_log_lvl Integer logging level.
 * @param logging_mutex Mutex to pass to logger.
 */
static void anukrta_setup_logging (int anu_log_lvl,
                                   pthread_mutex_t *logging_mutex) {

  int log_lvl = 0;
  int libav_log_lvl = 0;

  switch (anu_log_lvl) {
    case 3:
      {
        log_lvl = LOG_TRACE;
        libav_log_lvl = AV_LOG_VERBOSE;
        break;
      }
    case 2:
      {
        log_lvl = LOG_DEBUG;
        libav_log_lvl = AV_LOG_INFO;
        break;
      }
    case 1:
      {
        log_lvl = LOG_INFO;
        libav_log_lvl = AV_LOG_INFO;
        break;
      }
    default:
      {
        libav_log_lvl = AV_LOG_ERROR;
        log_lvl = LOG_ERROR;
        break;
      }
  }

  av_log_set_level(libav_log_lvl);
  log_set_level(log_lvl);
  log_set_lock(log_lock_callback, logging_mutex);
}

/**
 * @brief Context data passed to threads.
 */
typedef struct hash_tworker_ctx {
  /** Pointer to file queue that needs to be hashed. */
  anu_file_vec *files;

  /** Pointer to program configuration. */
  anu_config *config;

  /** Array of hashes that are produced during thread execution. */
  uint64_t *hashes;

  /** Array of timestamps for each hash. */
  uint64_t *frame_timestamps;

  /** Indices of files to be processed. */
  size_t *pending_indices;

  /** Number of files needing to be processed. */
  size_t pending_count;

  /** Index of file to process by thread worker. */
  atomic_size_t *current_idx; /* Shared index */

  /** Array of result codes from threads. */
  enum ANU_STATUS *results;
} hash_tworker_ctx;

static void *hash_worker_thread (void *arg) {
  hash_tworker_ctx *targs = (hash_tworker_ctx *) arg;

  const size_t segments = targs->config->segments;
  anu_config *config = targs->config;
  enum ANU_STATUS *results = targs->results;
  anu_file *files = targs->files->items;
  u64 *hashes = targs->hashes;
  u64 *timestamps = targs->frame_timestamps;
  _Atomic size_t *current_idx = targs->current_idx;

  for (;;) {
    /* Get index of next file in queue */
    size_t q_idx = atomic_fetch_add(current_idx, 1);

    /* Check if next index exceeds file count */
    if (q_idx >= targs->pending_count) {
      break;
    }

    /* Get actual file index from the queue index */
    size_t file_idx = targs->pending_indices[q_idx];

    size_t hash_off = file_idx * segments;

    /* Do the hashing and store return code */
    results[file_idx] = anu_video_hash(
        &files[file_idx], config, hashes + hash_off, timestamps + hash_off);
  }

  return NULL;
}

static void execute_hash_worker_threads (anu_config *config,
                                         hash_tworker_ctx *args,
                                         size_t file_count) {
  /* NOTE: Thread count should not exceed file count */
  size_t final_thread_count = MINIMUM(config->thread_count, file_count);
  log_info("Available threads: %zu, running with: %zu", config->thread_count,
           final_thread_count);
  config->thread_count = final_thread_count;

  assert(config->thread_count > 0);

  pthread_t *threads __free(ptr) =
      xcalloc(final_thread_count, sizeof(*threads));

  log_info("Starting %zu hashing threads...", final_thread_count);

  /* Create the threads */
  int threads_made = 0;
  for (size_t i = 0; i < final_thread_count; i++) {
    int success =
        (pthread_create(&threads[i], NULL, hash_worker_thread, args) == 0);
    threads_made += success;
    if (!success) {
      log_warn("Failed to create thread #%zu", i);
      break;
    }
  }

  log_debug("Spawned %d threads.", threads_made);

  /* Wait for all threads to finish */
  int threads_joined = 0;
  for (int i = 0; i < threads_made; i++) {
    int success = (pthread_join(threads[i], NULL) == 0);
    threads_joined += success;
    if (!success) {
      log_warn("Failed to join thread #%d", i);
    }
  }

  log_debug("Joined %d threads.", threads_joined);
}

static int anukrta_driver (anu_config *config) {

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
  enum ANU_STATUS *thread_results __free(ptr) = NULL;

  hashes = xmalloc(hash_collection_len * sizeof(*hashes));
  timestamps = xmalloc(hash_collection_len * sizeof(*timestamps));
  thread_results = xmalloc(file_count * sizeof(*thread_results));

  /* Open the database */
  anu_cache_ctx *db __free(cache_ctx) = NULL;

  /* File queue */
  size_t *pending_indices __free(ptr) = NULL;
  pending_indices = xcalloc(file_count, sizeof(size_t));
  size_t pending_count = 0;

  if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_CACHE)) {

    /* Initialise SQLite3 */
    cache_init_once();
    db = cache_open_db("cache.db");
    if (!db) {
      log_warn(
          "Failed to open cache database. Proceeding with caching disabled.");
      ANU_CLEAR_FLAG(config->runtime_flags, RT_CACHE);
    }

    log_info("Checking database cache for already hashed files...");
    for (size_t i = 0; i < file_count; i++) {
      anu_file *file = &kv_A(files, i);
      uint64_t row_id = 0;
      uint64_t file_duration_us = 0;

      /* If its already cached... */
      if (cache_is_file_valid(db, file, &row_id, &file_duration_us)) {
        size_t out_count = 0;
        size_t hash_off = i * config->segments;

        /* Retrieve the hashes & timestamps directly into our arrays */
        int ret =
            cache_get_hashes(db, row_id, config->segments, hashes + hash_off,
                             timestamps + hash_off, &out_count);

        /* Only use cache if the database contains the EXACT amount of segments we requested */
        if (ret == 0 && out_count == config->segments) {
          thread_results[i] = ANU_STATUS_FILE_CACHED;
          file->duration_us = (size_t) file_duration_us;
        }
      } else {
        pending_indices[pending_count++] = i;
      }
    }
  } else {
    for (size_t i = 0; i < file_count; i++) {
      pending_indices[pending_count++] = i;
    }
  }

  time_t curr_time = time(NULL);
  log_debug("Current time: %ld", curr_time);

  atomic_size_t current_file_idx = 0;

  /* Package the arguments */
  hash_tworker_ctx thread_ctx = {
    .files = &files,
    .config = config,
    .hashes = hashes,
    .frame_timestamps = timestamps,
    .results = thread_results,
    .pending_count = pending_count,
    .pending_indices = pending_indices,
    .current_idx = &current_file_idx,
  };

  if (pending_count > 0) {
    execute_hash_worker_threads(config, &thread_ctx, pending_count);
  } else {
    log_info("All %zu files already exist in cache. Skipping hashing phase.",
             file_count);
  }

  /* Cache the results (if caching enabled) */
  cache_sync_results_maybe(db, config, &files, thread_results, hashes,
                           timestamps);

  bk_node *filetree = NULL;

  for (size_t i = 0; i < file_count; i++) {
    /* Current file */
    anu_file *file = &kv_A(files, i);
    enum ANU_STATUS result = thread_results[i];
    size_t file_idx = (i * config->segments);
    /* Check the result saved by the thread */

    /* Failed to hash a file */
    if (result == ANU_IO_FAIL) {
      log_info("Failed to hash file '%s'", anu_file_get_filename(file));
      continue;
    }

    /* We skipped this file */
    if (result == ANU_STATUS_FILE_SKIPPED) {
      log_info("Skipped file '%s'", anu_file_get_filename(file));
      continue;
    }

    if (result == ANU_STATUS_FILE_CACHED) {
      log_info("File %s previously hashed (loaded from cache).",
               anu_file_get_filename(file));
    }

    if (result == ANU_OK || result == ANU_STATUS_FILE_CACHED) {

      for (size_t segment_off = 0; segment_off < config->segments;
           segment_off++) {
        size_t curr_seg_idx = file_idx + segment_off;
        u64 curr_hash = hashes[curr_seg_idx];
        bk_tree_insert(&filetree, curr_hash, i);
      }
    }
  }

  anu_report report = anu_generate_report(&files, hashes, config, filetree);
  anu_print_report(config, &report, &files, hashes);

  /* CLEANUP */
  anu_report_destroy(&report);
  bk_tree_node_free(filetree);

  /* Close the database */
  if (ANU_HAS_ANY_FLAG(config->runtime_flags, RT_CACHE)) {
    sqlite3_shutdown();
  }

  return 0;
}

int main (int argc, char *argv[]) {

  /* Retrieve default configuration */
  anu_config config = anukrta_default_config();

  /* Return code after parsing CLI options */
  int parsing_return = anu_cli_parse_options(&config, argc, argv);

  /* Exit if non zero return value OR
   * if config has exit_early flag set */
  if (parsing_return ||
      (ANU_HAS_ANY_FLAG(config.runtime_flags, RT_EXIT_EARLY))) {
    return parsing_return;
  }

  /* Logging Setup */
  pthread_mutex_t log_mutex;
  pthread_mutex_init(&log_mutex, NULL);

  int logging_level = ANU_GET_VERBOSITY(config.runtime_flags);
  if (logging_level > 0) {
    anu_cli_print_configuration(&config);
  }
  anukrta_setup_logging(logging_level, &log_mutex);

  /* Start of program */

  log_info("%s now running...", argv[0]);

  int driver_ret = anukrta_driver(&config);
  pthread_mutex_destroy(&log_mutex);
  return driver_ret;
}
