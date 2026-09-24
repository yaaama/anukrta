/**
 * @file main.c
 *
 * @brief Main file for `anukrta`.
 *
 *
 */

#include <assert.h>
#include <inttypes.h>
#include <libavutil/log.h>
#include <locale.h>
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
#include "signals.h"
#include "sqlite3.h"
#include "term.h"
#include "tree.h"
#include "ui.h"
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
 * @param ak_log_lvl Integer logging level for application.
 * @param logging_mutex Mutex to pass to logger.
 */
static void ak_logging_init (u32 ak_log_level, pthread_mutex_t *logging_mutex) {

  static const int ak_map[] = {0, LOG_ERROR, LOG_INFO, LOG_DEBUG, LOG_TRACE};
  static const int libav_map[] = {AV_LOG_QUIET, AV_LOG_ERROR, AV_LOG_INFO, AV_LOG_VERBOSE, AV_LOG_DEBUG};

  u32 safe_lvl = (ak_log_level >= 0 && ak_log_level <= AK_ARRAY_SIZE(ak_map)) ? ak_log_level : 0;

  av_log_set_level(libav_map[safe_lvl]);

  if (safe_lvl == 0) {
    log_set_quiet(true);
  } else {
    log_set_level(ak_map[safe_lvl]);
  }
  log_set_lock(log_lock_callback, logging_mutex);
}

/**
 * @brief Context data passed to threads.
 */
typedef struct hashing_thread_ctx {
  ak_file_v *files;            /**< Pointer to file queue that needs to be hashed. */
  ak_config *config;           /**< Pointer to program configuration. */
  ak_hash_entry *hash_entries; /**< Array of hash_entries */
  AK_STATUS *results;          /**< Array of result codes from threads. */
  ak_signals_ctx *signals;

  /** Indices of files to be processed.
   * E.g. Index 0 of this array may be '5' which means ak_file_v[5] requires processing. */
  size_t *pending_indices;
  atomic_size_t pending_count; /**< Number of elements in `pending_indices`. */

  atomic_size_t current_idx;     /**< Index of file to process by thread worker. */
  atomic_size_t completed_count; /**< Count for completed files */
} hashing_thread_ctx;

/**
 * @brief Worker thread routine for hashing video files.
 *
 * @details This function runs in a loop, fetching the next available file from
 * a shared work queue using an atomic counter (`current_idx`).
 * It maps the queue index to the actual file index, calculates the correct memory offset for the
 * resulting hashes, and performs the video hashing.
 * The thread terminates automatically when the work queue is exhausted.
 *
 * @param arg Pointer to `hashing_thread_ctx` struct.
 *
 * @return Always returns NULL.
 */
static void *hash_worker_thread (void *arg) {
  hashing_thread_ctx *targs = (hashing_thread_ctx *) arg;

  const size_t segments = targs->config->segments;
  enum AK_STATUS *results = targs->results;
  ak_file *files = targs->files->items;
  ak_hash_entry *hash_entries = targs->hash_entries;

  for (;;) {
    /* Get index of next file in queue */
    size_t q_idx = atomic_fetch_add(&targs->current_idx, 1);

    /* Check if next index exceeds file count */
    if (q_idx >= targs->pending_count) {
      break;
    }

    /* Get actual file index from the queue index */
    size_t file_idx = targs->pending_indices[q_idx];

    size_t entry_offset = (file_idx * segments);

    /* Do the hashing and store return code */
    results[file_idx] =
        ak_video_hash(&files[file_idx], targs->config, targs->signals, (hash_entries + entry_offset));
    atomic_fetch_add(&targs->completed_count, 1);
  }

  return NULL;
}

/**
 * Shutdown callback
 * This poisons the work queue.
 * (`q_idx >= pending_count`) will lead to exiting the hashing loop early.
 */
static void poison_hash_queue (int signo, void *userdata) {
  (void) signo;
  hashing_thread_ctx *tctx = userdata;
  /* Silence all libav output before quitting out of hashing */
  av_log_set_level(AV_LOG_QUIET);
  atomic_store(&tctx->pending_count, 0);
}

/**
 * Spawn and manage worker threads to hash pending files.
 *
 * Determines the optimal number of threads based on the number of available threads
 * and the number of pending files.
 * Initialises terminal context so we know if we are in interactive mode (TTY) or not.
 * Starts progress bar for the terminal if we are in a TTY, and then cleans it up.
 *
 * @param config Pointer to global configuration.
 * @param args Pointer to the thread context/arguments containing pending files, status tracking, and shared
 * counters.
 */
static void execute_hash_worker_threads (ak_config *config, hashing_thread_ctx *args) {
  /* Number of pending files to process */
  size_t file_count = args->pending_count;
  /* NOTE: Thread count should not exceed file count */
  size_t final_thread_count = MINIMUM(config->thread_count, file_count);
  log_info("Utilising [%zu/%zu] threads.", final_thread_count, config->thread_count);
  config->thread_count = final_thread_count;

  assert(config->thread_count > 0);

  pthread_t *threads AK_AUTO(free) = xcalloc(final_thread_count, sizeof(*threads));

  AK_AUTO(term_ctx) ak_term_ctx *term = xcalloc(1, sizeof(*term));
  AK_AUTO(ui_ctx) ak_ui_ctx *ui = xcalloc(1, sizeof(*ui));

  if (ak_term_ctx_init(term)) {
    log_error("Failed to initialise terminal context.");
  }

  ak_ui_ctx_init(config, term, ui);
  char *progress_label_str = "HASHING";
  ak_ui_progress_start(ui, &args->completed_count, args->pending_count, progress_label_str,
                       STRLEN("HASHING"));

  /* Spawn hashing worker threads */
  int threads_made = 0;

  for (size_t i = 0; i < final_thread_count; i++) {
    int success = (pthread_create(&threads[i], NULL, hash_worker_thread, args) == 0);
    threads_made += success;
    if (!success) {
      log_warn("Failed to create thread #%zu", i);
      break;
    }
  }

  log_info("Spawned '%d' worker threads.", threads_made);

  /* Wait for all threads to finish */
  int threads_joined = 0;
  for (int i = 0; i < threads_made; i++) {
    int success = (pthread_join(threads[i], NULL) == 0);
    threads_joined += success;
    if (!success) {
      log_warn("Failed to join thread #%d", i);
    }
  }

  /* Stop progress bar and clear it up */
  ak_ui_progress_stop(ui);
  log_debug("Joined '%d' threads.", threads_joined);
}

/* Tries to load a single file from cache. */
static bool search_cache_for_file (anu_cache_ctx *db,
                                   size_t segments_needed,
                                   ak_file *file,
                                   size_t file_idx,
                                   ak_hash_entry *hash_entries) {
  u64 row_id = 0;
  i64 duration = 0;

  if (!cache_is_file_valid(db, file, &row_id, &duration)) {
    return false;
  }

  size_t out_count = 0;
  size_t entry_offset = (file_idx * segments_needed);

  int ret = cache_get_hashes(db, row_id, segments_needed, (hash_entries + entry_offset), &out_count);

  if ((ret != 0) || (out_count != segments_needed)) {
    return false;
  }

  file->duration_us = duration;
  return true;
}

/**
 * Main driver for program.
 *
 * We conduct most of our business logic here:
 * - Search for files and collect the files we are interested in.
 * - Check database for files that are already hashed.
 * - Retrieve cached hashes or queue newly discovered files for hashing.
 * - Spin up threads to hash pending files.
 * - Build BK Tree of the file hashes.
 * - Generate and print report of the run.
 *
 * @param config Pointer to the configuration settings.
 * @param paths  Pointer to the dynamic array of paths to scan.
 *
 * @return 0 on success, or a non-zero error code if initialization/scanning fails.
 */
static int anukrta_driver (ak_config *config, ak_paths *paths, ak_signals_ctx *signals) {

  assert(config->segments > 0);

  /* Initialise file list */
  ak_file_v files AK_AUTO(file_v) = {0};
  kv_ensure_space(files, 64); /* start off with 64 elements */

  /* Scan path(s) and store in files queue */
  ak_explore_scan_paths(config, paths, &files);

  /* Exit early if we do not find any files */
  const usize file_count = kv_size(files);
  if (file_count == 0) {
    log_warn("No video files found!");
    return -1;
  }
  log_info("Found `%zu` files", file_count);

  /* The total number of segments to hash = number of files * number of segments */
  const usize segments_count = (file_count * config->segments);
  log_info("Total segments to process: (%zu * %zu) = `%zu`", file_count, config->segments, segments_count);

  /* List of hash entries (each segment has a hash entry) */
  ak_hash_entry *hash_entries AK_AUTO(free) = xmalloc(segments_count * sizeof(*hash_entries));

  /* Status of each file */
  enum AK_STATUS *file_statuses AK_AUTO(free) = xmalloc(file_count * sizeof(*file_statuses));

  /* File queue */
  size_t *pending_indices AK_AUTO(free) = xcalloc(file_count, sizeof(*pending_indices));
  size_t pending_count = 0;

  /* Database context, will remain NULL if caching is disabled */
  anu_cache_ctx *cache_ctx AK_AUTO(cache_ctx) = NULL;

  bool cache_enabled = ak_flag_has(config->runtime_flags, RT_CACHE);

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
    log_debug("Checking database cache for already hashed files...");

    for (size_t i = 0; i < file_count; i++) {

      ak_file *file = &kv_A(files, i);
      if (search_cache_for_file(cache_ctx, config->segments, file, i, hash_entries)) {
        /* If file is successfully loaded from cache mark it as so */
        file_statuses[i] = AK_STATUS_FILE_CACHED;
      } else {
        /* Else add it to our work queue */
        file_statuses[i] = AK_STATUS_FILE_PENDING;
        pending_indices[pending_count] = i;
        ++pending_count;
      }
    }
  } else {
    /* If caching is enabled but cache db was not opened, then we print an error */
    if (cache_enabled) {
      log_error("Failed to open cache database! Proceeding with caching disabled.");
    }

    config->runtime_flags &= ~RT_CACHE;

    /* Add all files found to our work queue and set their status as PENDING */
    for (size_t i = 0; i < file_count; i++) {
      pending_indices[i] = i;
      file_statuses[i] = AK_STATUS_FILE_PENDING;
    }
    pending_count = file_count;
  }

  atomic_size_t current_file_idx = 0;
  atomic_size_t completed_count = 0;

  /* Package the arguments */
  hashing_thread_ctx thread_ctx = {
    .files = &files,
    .config = config,
    .hash_entries = hash_entries,
    .results = file_statuses,
    .signals = signals,
    .pending_count = pending_count,
    .pending_indices = pending_indices,
    .current_idx = current_file_idx,
    .completed_count = completed_count,
  };
  atomic_init(&thread_ctx.current_idx, 0);
  atomic_init(&thread_ctx.completed_count, 0);

  /* Register the shutdown callback BEFORE the pre-spawn check below.
   * Ordering matters:
   *   - Signal arrives before registration  -> caught by the check below.
   *   - Signal arrives after registration   -> queue gets poisoned.
   */
  signals->on_shutdown_data = &thread_ctx;
  atomic_store(&signals->on_shutdown, poison_hash_queue);

  /* A signal may have arrived before the callback was registered. Nothing
   * has been hashed yet, so there is nothing new to cache, just exit. */
  if (ak_shutdown_requested(signals)) {
    atomic_store(&signals->on_shutdown, NULL);
    return ak_shutdown_exit_code(signals);
  }
  if (pending_count > 0) {
    execute_hash_worker_threads(config, &thread_ctx);
  } else {
    log_info("All %zu files already exist in cache, Skipping hashing phase.", file_count);
  }
  atomic_store(&signals->on_shutdown, NULL);

  /* Interrupted mid-hash: skip the tree/report phase, but STILL sync the
   * cache - files that finished carry AK_OK and get saved, so the next run
   * resumes where this one stopped.
   * All remaining resources (files vector, hash_entries, cache_ctx) unwind via
   * their AK_AUTO destructors. */
  if (ak_shutdown_requested(signals)) {
    log_warn("Shutdown requested (signal %d). Saved completed work, exiting.",
             atomic_load(&signals->received));
    cache_sync_results_maybe(cache_ctx, config, &files, file_statuses, hash_entries);
    return ak_shutdown_exit_code(signals);
  }

  /* Cache the results (if caching enabled) */
  cache_sync_results_maybe(cache_ctx, config, &files, file_statuses, hash_entries);

  bk_node *hash_tree = NULL;

  for (size_t i = 0; i < file_count; i++) {
    /* Current file */
    ak_file *file = &kv_A(files, i);
    enum AK_STATUS result = file_statuses[i];
    size_t segment_start_idx = (i * config->segments);
    /* Check the result saved by the thread */

    /* Failed to hash a file */
    if (result == AK_IO_FAIL) {
      log_error("Failed to hash %s", file->path);
      continue;
    }

    /* We skipped this file for being too short */
    if (result == AK_SKIP_SHORT_DURATION) {
      continue;
    }

    /* Some other error occured */
    if ((result != AK_OK) && (result != AK_STATUS_FILE_CACHED)) {
      log_error("Some other error occured for '%s'", ak_file_name(file));
      continue;
    }

    /* File was loaded from cache */
    if (result == AK_STATUS_FILE_CACHED) {
      log_trace("`%s` was loaded from cache.", ak_file_name(file));
    }

    /* Add items to bk hash */
    for (size_t segment_off = 0; segment_off < config->segments; segment_off++) {
      size_t curr_seg_idx = segment_start_idx + segment_off;
      u64 curr_hash = hash_entries[curr_seg_idx].hash;
      bk_tree_insert(&hash_tree, curr_hash, i);
    }
  }

  /* Generate report */
  ak_report report = ak_report_build(&files, file_statuses, hash_entries, config, hash_tree);
  /* Print report */
  ak_report_print(config, &report, &files, file_statuses, hash_entries);

  /* CLEANUP */
  ak_report_destroy(&report);
  bk_tree_node_free(hash_tree);

  /* Close the database */
  if (cache_enabled) {
    sqlite3_shutdown();
  }

  return 0;
}

/**
 * Configure locales to safe values.
 *
 * Enforce predictable formatting/sorting/string behaviour by overriding hosts locale settings.
 * - LC_NUMERIC: Force usage of '.' as decimal seperator.
 * - LC_COLLATE: Force standard ASCII/byte-order string comparisons and collation.
 *
 * @warn We modify global state and therefore this function is not threadsafe.
 * We should always invoke this very EARLY in the program.
 *
 * @return 0 on success, or -1 if any `setlocale()` call fails.
 */
static int setup_locales (void) {
  /* NOLINTBEGIN (concurrency-mt-unsafe) */

  /* Force numbers and math back to the safe "C" standard.
   * Prevents the decimal/comma bug when parsing/printing numbers (floats) */
  if (setlocale(LC_NUMERIC, "C") == NULL) {
    fprintf(stderr, "Failed to set locale LC_NUMERIC\n");
    return -1;
  }

  /* Force sorting/comparisons to C so string comparisons are predictable */
  if (setlocale(LC_COLLATE, "C") == NULL) {
    fprintf(stderr, "Failed to set locale LC_COLLATE\n");
    return -1;
  }

  return 0;
  /* NOLINTEND */
}

AK_DEFINE_AUTO(argv_paths, ak_paths, kv_destroy(*ak__obj))

/**
 * @brief Application entry point.
 *
 * Sets up safe locales, parses CLI arguments, initializes logging,
 * and then hands off execution to the main driver function.
 *
 * @param argc
 * @param argv
 *
 * @return 0 for success, non-zero for failure.
 */
int main (int argc, char *argv[]) {

  if (setup_locales()) {
    return -1;
  }
  ak_signals_ctx signals;
  if (ak_signals_install(&signals) != 0) {
    fprintf(stderr, "Failed to install signal handling\n");
    return -1;
  }

  /* Retrieve default configuration */
  ak_config config = anukrta_default_config();

  /*
   * Parsing occurs in 2 phases:
   * Phase 1: Tokenise argv into a list of option events (no side effects).
   * Phase 2: apply the events to the config and find out what to do next using
   * the return value of ak_cli_tokenize.
   */
  ak_cli_events events AK_AUTO(cli_events) = KV_INITIAL_VALUE; /**< Event vector parsed from argv. */
  ak_paths paths AK_AUTO(argv_paths) = KV_INITIAL_VALUE;       /**< Paths from argv (if any). */
  ak_cli_action action = AK_CLI_RUN;                           /**< What we should do after parsing argv. */

  if (ak_cli_tokenize(argc, argv, &events, stderr) != 0) {
    /* We've run into an error, we should exit with failure code. */
    return EXIT_FAILURE;
  }
  action = ak_cli_apply(&config, &events, argv[0], &paths);

  /* If we're told to exit by CLI parser... */
  if (action != AK_CLI_RUN) {
    return (action == AK_CLI_EXIT_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  /* Logging Setup */
  pthread_mutex_t log_mutex;
  pthread_mutex_init(&log_mutex, NULL);

  if (config.verbosity > 0) {
    ak_cli_print_config(stdout, &config);
  }
  ak_logging_init(config.verbosity, &log_mutex);

  /* Start of program */
  log_debug("%s now running...", argv[0]);

  int driver_ret = anukrta_driver(&config, &paths, &signals);
  pthread_mutex_destroy(&log_mutex);
  kv_destroy(paths);

  return driver_ret;
}
