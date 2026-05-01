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
  atomic_size_t *current_idx; /* Shared index */
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

static void *hash_worker_thread (void *arg) {
  worker_args *targs = (worker_args *) arg;

  const size_t file_count = targs->file_count;
  const size_t segments = (size_t) targs->config->segments;

  while (1) {
    size_t my_idx;
    my_idx = atomic_fetch_add(targs->current_idx, 1);

    if (my_idx >= file_count) {
      break;
    }

    /* Get the specific file and hash pointer for this index */
    anu_file *file = (targs->files->items + my_idx);
    size_t hash_idx = my_idx * segments;
    uint64_t *my_hashes = &targs->hashes[hash_idx];

    log_trace("Thread %lu processing file index %zu", pthread_self(), my_idx);

    /* Do the hashing */
    targs->results[my_idx * CACHE_STRIDE_INT] =
        anu_video_hash(file, targs->config, my_hashes);
  }

  return NULL;
}

static int anukrta_driver (anukrta_config *config) {

  /* Store the files we find in the path */
  anu_file_q files;
  /* Initialise the list to hold files we find */
  anu_fileq_init(&files, 16);

  scan_dirs(config, &files);

  const size_t file_count = files.count;
  if (file_count < 1) {
    log_warn("No video files found!");
    anu_fileq_destroy(&files);
    return -1;
  }
  assert(config->segments > 0);
  const size_t segments_st = config->segments;

  log_info("Found `%zu` files", file_count);

  const size_t hash_collection_len = (file_count * segments_st);

  /* Array of hashes
   * E.g. (N files with 2 segments) would look like this:
   * [ File1Seg1, File1Seg2, File2Seg1, File2Seg2, ... File N Seg 2 ]
   * FileNSegN would be the hash created for that segment
   */
  uint64_t *hashes = calloc(hash_collection_len, sizeof(*hashes));
  if (!hashes) {
    ANU_DIE("Failed to allocate memory.");
  }

  int *results = calloc((file_count * CACHE_STRIDE_INT), sizeof(*results));
  if (!results) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* THREADING START */

  /* NOTE: Thread count should not exceed file count or it is a waste of resources */
  config->thread_count = MINIMUM(config->thread_count, file_count);
  assert(config->thread_count > 0);
  pthread_t *threads = calloc(config->thread_count, sizeof(*threads));
  if (!threads) {
    ANU_DIE("Failed to allocate memory for threads");
  }

  atomic_size_t current_file_idx = 0;

  /* Package the arguments */
  worker_args args = {
    .files = &files,
    .config = config,
    .hashes = hashes,
    .results = results,
    .file_count = file_count,
    .current_idx = &current_file_idx,
  };

  log_info("Starting %zu hashing threads...", config->thread_count);

  /* Create the threads */
  int threads_made = 0;
  for (size_t i = 0; i < config->thread_count; i++) {
    int success =
        (pthread_create(&threads[i], NULL, hash_worker_thread, &args) == 0);
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

  anu_file *file;
  bk_node *filetree = NULL;

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);
    int result = results[i * CACHE_STRIDE_INT];
    /* Check the result saved by the thread */
    if (result == -1) {
      log_debug("Failed to hash file %s", anu_file_get_filename(file));
    }
    if (result == -2) {
      /* We skipped this hash so lets move onto the next file. */
      log_trace("Skipped over file %s", anu_file_get_filename(file));
    }

    if (result == 0) {
      for (size_t j = 0; j < segments_st; j++) {
        bk_tree_insert(&filetree, hashes[(i * segments_st) + j], i);
      }
    }
  }

  anu_report report = anu_generate_report(&files, hashes, config, filetree);
  anu_print_report(config, &report, &files, hashes);

  /* CLEANUP */
  anu_report_destroy(&report);
  bk_tree_node_free(filetree);
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
    .thread_count = 1,
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
  pthread_mutex_destroy(&log_mutex);
  return driver_ret;
}
