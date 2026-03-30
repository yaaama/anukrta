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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "explore.h"
#include "log.h"
#include "report.h"
#include "tree.h"
#include "util.h"
#include "video.h"

typedef struct {
  anu_file_q *files;
  anukrta_config *config;
  uint64_t *hashes;
  int *results; /* To store the return value of hash_video */
  size_t file_count;
  size_t *current_idx;    /* Shared index */
  pthread_mutex_t *mutex; /* Protects current_idx */
} worker_args;

/* callback function for logger */
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

static void scan_dirs (anukrta_config *config, anu_file_q *files) {
  /* Scan current directory */
  if (config->scan_curr_dir) {
    log_info("Scanning current directory");
    if (anu_file_recursive_filewalk(".", files)) {
      log_warn("Error searching for files in current directory.");
    }
    return;
  }

  /* Else we scan paths given */
  for (size_t i = 0; i < config->paths_count; i++) {
    /* FIXME: Ensure paths do not point to the same place */
    if (anu_file_recursive_filewalk(config->paths[i], files)) {
      log_warn("Error searching for files in '%s'", config->paths[i]);
    }
  }
}

static void *hash_worker_thread (void *arg) {
  worker_args *targs = (worker_args *) arg;

  while (1) {
    size_t my_idx;

    /* --- CRITICAL SECTION --- */
    /* Lock the mutex to safely grab the next file index */
    pthread_mutex_lock(targs->mutex);
    my_idx = *(targs->current_idx);
    if (my_idx < targs->file_count) {
      *(targs->current_idx) += 1; /* Advance the queue for the next thread */
    }
    pthread_mutex_unlock(targs->mutex);
    /* ------------------------ */

    /* If we've processed all files, exit the thread */
    if (my_idx >= targs->file_count) {
      break;
    }

    /* Get the specific file and hash pointer for this index */
    anu_file *file = (targs->files->items + my_idx);
    uint64_t *my_hashes = &targs->hashes[my_idx * targs->config->segments];

    log_trace("Thread %lu processing file index %zu", pthread_self(), my_idx);

    /* Do the hashing */
    targs->results[my_idx] = anu_video_hash(file, targs->config, my_hashes);
  }

  return NULL;
}

static int anukrta_driver (anukrta_config *config) {

  /* Store the files we find in the path */
  anu_file_q files;
  /* Initialise the list to 20 items */
  anu_fileq_init(&files, 20);

  scan_dirs(config, &files);

  size_t file_count = files.count;

  if (file_count < 1) {
    log_warn("No video files found!");
    anu_fileq_destroy(&files);
    return -1;
  }

  log_info("Found `%zu` files", file_count);

  size_t hash_collection_len = (file_count * config->segments);

  /* Array of hashes
   * E.g. (N files with 2 segments) would look like this:
   * [ File1Seg1, File1Seg2, File2Seg1, File2Seg2, ... File N Seg 2 ]
   * FileNSegN would be the hash created for that segment
   */
  uint64_t *hashes;
  hashes = calloc(hash_collection_len, sizeof(*hashes));
  if (!hashes) {
    ANU_DIE("Failed to allocate memory.");
  }

  int *results;
  results = calloc(file_count, sizeof(*results));
  if (!results) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* THREADING START */

  /* NOTE: Thread count should not exceed file count or it is a waste of resources */
  config->thread_count = MINIMUM(config->thread_count, file_count);
  pthread_t *threads = calloc(config->thread_count, sizeof(pthread_t));
  if (!threads) {
    ANU_DIE("Failed to allocate memory for threads");
  }

  pthread_mutex_t idx_mutex;
  pthread_mutex_init(&idx_mutex, NULL);

  size_t current_file_idx = 0;

  /* Package the arguments */
  worker_args args = {.files = &files,
                      .config = config,
                      .hashes = hashes,
                      .results = results,
                      .file_count = file_count,
                      .current_idx = &current_file_idx,
                      .mutex = &idx_mutex};

  log_info("Starting %zu hashing threads...", config->thread_count);

  /* Create the threads */
  for (size_t i = 0; i < config->thread_count; i++) {
    if (pthread_create(&threads[i], NULL, hash_worker_thread, &args) != 0) {
      log_warn("Failed to create thread %zu", i);
    }
  }

  /* Wait for all threads to finish */
  for (size_t i = 0; i < config->thread_count; i++) {
    pthread_join(threads[i], NULL);
  }

  /* Clean up threading resources */
  pthread_mutex_destroy(&idx_mutex);

  anu_file *file;
  bk_tree filetree = {0};

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);
    int result = results[i];
    /* Check the result saved by the thread */
    if (result == -1) {
      log_debug("Failed to hash file %s", anu_file_get_filename(file));
    }
    if (result == -2) {
      /* We skipped this hash so lets move onto the next file. */
      log_trace("Skipped over file %s", anu_file_get_filename(file));
    }

    if (result == 0) {
      for (size_t j = 0; j < config->segments; j++) {
        bk_tree_insert(&filetree, hashes[(i * config->segments) + j], i);
      }
    }
  }

  anu_report report = anu_generate_report(&files, hashes, config, &filetree);
  anu_print_report(config, &report, &files, hashes);

  /* CLEANUP */
  anu_report_destroy(&report);
  bk_tree_node_free(filetree.root);
  anu_fileq_destroy(&files);
  free(hashes);
  free(results);
  free(threads);

  return 0;
}

int main (int argc, char *argv[]) {

  /* Default configuration */
  anukrta_config config = {
    .dry_run = 0,
    .verbose = 0,
    .segments = 2,
    .threshold = 15,
    .hash_algorithm = ANU_HASH_ALGO_DCT,
    .skip_duration = 3,
    .thread_count = ANU_DEF_THREAD_COUNT,
    .detect_bars = 1,
    .detect_black_frames = 1,
  };

  /* Option parsing return value */
  int parsing_return = anu_cli_parse_options(&config, argc, argv);

  /* Exit if non zero or if config has exit_early flag set */
  if (parsing_return || config._exit_early) {
    return parsing_return;
  }

  /* Logging Setup */
  pthread_mutex_t log_mutex;
  pthread_mutex_init(&log_mutex, NULL);

  int log_lvl = 0;
  int libav_log_lvl = 0;
  if (config.verbose) {
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

  return driver_ret;
}
