/* Video similarity tool */

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
static void anukrta_setup_logging (int anu_log_lvl, pthread_mutex_t *logging_mutex) {

  static const int anu_map[] = {LOG_ERROR, LOG_INFO, LOG_DEBUG, LOG_TRACE};
  static const int libav_map[] = {AV_LOG_ERROR, AV_LOG_INFO, AV_LOG_INFO, AV_LOG_VERBOSE};

  int safe_lvl = (anu_log_lvl >= 0 && anu_log_lvl <= 3) ? anu_log_lvl : 0;

  av_log_set_level(libav_map[safe_lvl]);
  log_set_level(anu_map[safe_lvl]);
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
  /** Array of hash_entries (stores hash + timestamp for hash) */
  hash_entry *hash_entries;
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
  enum ANU_STATUS *results = targs->results;
  anu_file *files = targs->files->items;
  hash_entry *hash_entries = targs->hash_entries;
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

    size_t entry_offset = (file_idx * segments);

    /* Do the hashing and store return code */
    results[file_idx] = anu_video_hash(&files[file_idx], targs->config, (hash_entries + entry_offset));
  }

  return NULL;
}

static void execute_hash_worker_threads (anu_config *config, hash_tworker_ctx *args, size_t file_count) {
  /* NOTE: Thread count should not exceed file count */
  size_t final_thread_count = MINIMUM(config->thread_count, file_count);
  log_info("Available threads: %zu, utilising %zu of them", config->thread_count, final_thread_count);
  config->thread_count = final_thread_count;

  assert(config->thread_count > 0);

  pthread_t *threads __free(ptr) = xcalloc(final_thread_count, sizeof(*threads));

  log_debug("Starting %zu hashing threads...", final_thread_count);

  /* Create the threads */
  int threads_made = 0;
  for (size_t i = 0; i < final_thread_count; i++) {
    int success = (pthread_create(&threads[i], NULL, hash_worker_thread, args) == 0);
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

/* Tries to load a single file from cache. */
static ALWAYS_INLINE bool anu_try_load_from_cache (anu_cache_ctx *db,
                                                   size_t segments_needed,
                                                   anu_file *file,
                                                   size_t file_idx,
                                                   hash_entry *hash_entries) {
  uint64_t row_id = 0;
  uint64_t duration = 0;

  if (!cache_is_file_valid(db, file, &row_id, &duration)) {
    return false;
  }

  size_t out_count = 0;
  size_t entry_offset = (file_idx * segments_needed);

  int ret = cache_get_hashes(db, row_id, segments_needed, (hash_entries + entry_offset), &out_count);

  if ((ret != 0) || (out_count != segments_needed)) {
    return false;
  }

  file->duration_us = (size_t) duration;
  return true;
}

/**
 * Main driver for program.
 *
 * We conduct most of our business logic here:
 * - Search for files and collect the files we are interested in.
 * - Check for files that are already hashed.
 * - Retrieve cached hashes.
 * - Hash new files.
 * - Print report of the run.
 *
 * @param config
 *
 * @return
 */
static int anukrta_driver (anu_config *config) {

  assert(config->segments > 0);

  /* Initialise file list */
  anu_file_vec files __free(anu_file_vec) = {0};
  kv_ensure_space(files, 64); /* start off with 64 elements */

  /* Scan path(s) and store in files queue */
  anu_explore_scan_directories(config, &files);

  /* Exit early if we do not find any files */
  const usize file_count = kv_size(files);
  if (file_count == 0) {
    log_warn("No video files found!");
    return -1;
  }
  log_info("Found `%zu` files", file_count);

  /* The total number of segments to hash = number of files * number of segments */
  const usize segments_count = (file_count * config->segments);
  log_debug("Total segments to process: (%zu * %zu) = `%zu`", file_count, config->segments, segments_count);

  /* List of hash entries (each segment has a hash entry) */
  hash_entry *hash_entries __free(ptr) = xmalloc(segments_count * sizeof(*hash_entries));

  /* Status of each file */
  enum ANU_STATUS *file_statuses __free(ptr) = xmalloc(file_count * sizeof(*file_statuses));

  /* File queue */
  size_t *pending_indices __free(ptr) = xcalloc(file_count, sizeof(*pending_indices));
  size_t pending_count = 0;

  /* Database context, will remain NULL if caching is disabled */
  anu_cache_ctx *cache_ctx __free(cache_ctx) = NULL;

  bool cache_enabled = ANU_HAS_ANY_FLAG(config->runtime_flags, RT_CACHE);
  /* Setup sqlite3 for use if caching is enabled */
  if (cache_enabled) {
    log_debug("Initialising SQLite3 library and opening database");
    cache_init_once();
    cache_ctx = cache_open_db("cache.db");
  }

  /* If caching is enabled:
   * TODO Check for # of files stored in database and only run loop if > 0
   */
  if (cache_ctx) {
    log_info("Checking database cache for already hashed files...");

    for (size_t i = 0; i < file_count; i++) {

      anu_file *file = &kv_A(files, i);
      if (anu_try_load_from_cache(cache_ctx, config->segments, file, i, hash_entries)) {
        /* If file is successfully loaded from cache mark it as so */
        file_statuses[i] = ANU_STATUS_FILE_CACHED;
      } else {
        /* Else add it to our work queue */
        file_statuses[i] = ANU_STATUS_FILE_PENDING;
        pending_indices[pending_count] = i;
        ++pending_count;
      }
    }
  } else {
    /* If caching is enabled but cache db was not opened, then we print an error */
    if (cache_enabled) {
      log_error("Failed to open cache database! Proceeding with caching disabled.");
    }

    ANU_CLEAR_FLAG(config->runtime_flags, RT_CACHE);

    /* Add all files found to our work queue */
    for (size_t i = 0; i < file_count; i++) {
      pending_indices[i] = i;
    }
    pending_count = file_count;
  }

  time_t curr_time = time(NULL);
  log_debug("Current time: %ld", curr_time);

  atomic_size_t current_file_idx = 0;

  /* Package the arguments */
  hash_tworker_ctx thread_ctx = {
    .files = &files,
    .config = config,
    .hash_entries = hash_entries,
    .results = file_statuses,
    .pending_count = pending_count,
    .pending_indices = pending_indices,
    .current_idx = &current_file_idx,
  };

  if (pending_count > 0) {
    execute_hash_worker_threads(config, &thread_ctx, pending_count);
  } else {
    log_info("All %zu files already exist in cache, Skipping hashing phase.", file_count);
  }

  /* Cache the results (if caching enabled) */
  cache_sync_results_maybe(cache_ctx, config, &files, file_statuses, hash_entries);

  bk_node *hash_tree = NULL;

  for (size_t i = 0; i < file_count; i++) {
    /* Current file */
    anu_file *file = &kv_A(files, i);
    enum ANU_STATUS result = file_statuses[i];
    size_t segment_start_idx = (i * config->segments);
    /* Check the result saved by the thread */

    /* Failed to hash a file */
    if (result == ANU_IO_FAIL) {
      log_error("Failed to hash %s", file->path);
      continue;
    }

    /* We skipped this file for being too short */
    if (result == ANU_SKIPPED_SHORT_DURATION) {
      continue;
    }

    /* Some other error occured */
    if ((result != ANU_OK) && (result != ANU_STATUS_FILE_CACHED)) {
      log_error("Some other error occured for '%s'", anu_file_get_filename(file));
      continue;
    }

    /* File was loaded from cache */
    if (result == ANU_STATUS_FILE_CACHED) {
      log_debug("`%s` was loaded from cache.", anu_file_get_filename(file));
    }

    /* Add items to bk hash */
    for (size_t segment_off = 0; segment_off < config->segments; segment_off++) {
      size_t curr_seg_idx = segment_start_idx + segment_off;
      u64 curr_hash = hash_entries[curr_seg_idx].hash;
      bk_tree_insert(&hash_tree, curr_hash, i);
    }
  }

  /* Generate report */
  anu_report report = anu_generate_report(&files, file_statuses, hash_entries, config, hash_tree);
  /* Print report */
  anu_print_report(config, &report, &files, file_statuses, hash_entries);

  /* CLEANUP */
  anu_report_destroy(&report);
  bk_tree_node_free(hash_tree);

  /* Close the database */
  if (cache_enabled) {
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
  if (parsing_return || (ANU_HAS_ANY_FLAG(config.runtime_flags, RT_EXIT_EARLY))) {
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
