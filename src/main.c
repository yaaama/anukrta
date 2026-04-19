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

/* Number of integers that fit in a cache line */
#define CACHE_STRIDE_INT (CACHE_LINE_SIZE / sizeof(int))

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

/* Helper function for quick-sort
 * Compares 'a' with 'b' lexicographically using its ASCII values
 * e.g. a="Hello" , b="Hi"
 * (H - H) = 0
 * (e - i) --> (101 - 105) = -4 => 'b' is lexicographically before 'a'  */
static inline int compare_strings (const void *a, const void *b) {
  return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* TODO Add a check for hard linked files (files with same inode number) */
static void scan_dirs (anukrta_config *config, anu_file_q *files) {
  /* Scan current directory */
  if (config->scan_curr_dir) {

    char *resolved = realpath(".", NULL);
    if (!resolved) {
      ANU_DIE("Could not resolve current path.");
    }

    log_info("Scanning current directory");
    if (anu_file_recursive_filewalk(resolved, files)) {
      log_warn("Error searching for files in current directory.");
    }

    free(resolved);
    return;
  }

  /* If we're not scanning current dir, then paths_count should be non zero */
  assert(config->paths_count > 0);

  /* Array to hold resolved absolute paths */
  char **real_paths =
      (char **) calloc(config->paths_count, sizeof(*real_paths));
  if (!real_paths) {
    ANU_DIE("Failed to allocate memory for paths.");
  }

  size_t valid_paths = 0;
  for (size_t i = 0; i < config->paths_count; i++) {
    /* realpath with NULL automatically allocates memory for the resolved path */
    char *resolved = realpath(config->paths[i], NULL);
    if (resolved != NULL) {
      real_paths[valid_paths++] = resolved;
    } else {
      log_warn("Could not resolve path '%s'", config->paths[i]);
    }
  }

  if (valid_paths == 0) {
    log_warn("No valid paths");
    free((void *) real_paths);
    return;
  }

  /* Sort paths lexicographically
   * so "/a/b" will be sorted before "/a/b/c"
   * We can then remove redundant paths
   */
  qsort((void *) real_paths, valid_paths, sizeof(char *), compare_strings);

  size_t unique_paths = 1;

  for (size_t i = 1; i < valid_paths; i++) {
    const char *prev = real_paths[unique_paths - 1];
    const char *current = real_paths[i];
    size_t prev_len = strlen(prev);

    bool is_duplicate_or_subdir = false;

    /* Check if 'current' starts with 'prev' */
    if (strncmp(prev, current, prev_len) == 0) {

      /* Ensure it's an exact match or an actual subdirectory,
       * avoiding similar names (e.g. prev="/dir", curr="/dir-2") */
      if (current[prev_len] == '\0' || current[prev_len] == '/' ||
          (prev[0] == '/' &&
           prev_len == 1)) { /* Handle cases where path is '/' */
        is_duplicate_or_subdir = true;
      }
    }

    /* If we found a redundant path */
    if (is_duplicate_or_subdir) {
      log_debug(
          "Skipping overlapping or duplicate path: '%s' (covered by '%s')",
          current, prev);
      free(real_paths[i]); /* Free the redundant path */
    } else {
      real_paths[unique_paths] = real_paths[i]; /* Keep the unique path */
      ++unique_paths;
    }
  }

  /* Now we scan only the unique paths */
  for (size_t i = 0; i < unique_paths; i++) {
    if (anu_file_recursive_filewalk(real_paths[i], files)) {
      log_warn("Error searching for files in '%s'", real_paths[i]);
    }
    /* Free path once we're done */
    free(real_paths[i]);
  }

  free((void *) real_paths);
}

static void *hash_worker_thread (void *arg) {
  worker_args *targs = (worker_args *) arg;

  const size_t file_count = targs->file_count;

  while (1) {
    size_t my_idx;
    my_idx = atomic_fetch_add(targs->current_idx, 1);

    if (my_idx >= file_count) {
      break;
    }

    /* Get the specific file and hash pointer for this index */
    anu_file *file = (targs->files->items + my_idx);
    uint64_t *my_hashes = &targs->hashes[my_idx * targs->config->segments];

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
  results = malloc(file_count * CACHE_STRIDE_INT * sizeof(*results));
  if (!results) {
    ANU_DIE("Failed to allocate memory.");
  }

  /* THREADING START */

  /* NOTE: Thread count should not exceed file count or it is a waste of resources */
  config->thread_count = MINIMUM(config->thread_count, file_count);
  pthread_t *threads;
  threads = calloc(config->thread_count, sizeof(*threads));
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
  for (int i = 0; i < (int) config->thread_count; i++) {
    int success =
        (pthread_create(&threads[i], NULL, hash_worker_thread, &args) == 0);
    threads_made += success;
    if (!success) {
      log_warn("Failed to create thread #%d", i);
      break;
    }
  }

  /* Wait for all threads to finish */
  for (int i = 0; i < threads_made; i++) {
    pthread_join(threads[i], NULL);
  }

  anu_file *file;
  bk_node *filetree = calloc(1, sizeof(*filetree));

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
      for (size_t j = 0; j < config->segments; j++) {
        bk_tree_insert(filetree, hashes[(i * config->segments) + j], i);
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
