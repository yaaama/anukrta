/* Video similarity tool */

#ifdef ANU_DEBUG
#    pragma message "Compilation in DEBUG mode."
#endif

#include <assert.h>
#include <inttypes.h>
#include <libavutil/log.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "explore.h"
#include "hash.h"
#include "log.h"
#include "report.h"
#include "tree.h"
#include "util.h"
#include "video.h"

static void scan_dirs (anukrta_config *config, anu_file_q *files) {
  if (config->scan_curr_dir) {
    log_info("Scanning current directory");
    if (anu_recursive_filewalk(".", files)) {
      log_warn("Error searching for files in current directory.");
    }

  } else {
    for (int i = 0; i < config->paths_count; i++) {
      if (anu_recursive_filewalk(config->paths[i], files)) {
        log_warn("Error searching for files in '%s'", config->paths[i]);
      }
    }
  }
}

int anukrta_driver (anukrta_config *config) {

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

  /* Array of hashes */
  size_t hash_collection_len = (file_count * config->segments);
  uint64_t *hashes = calloc(hash_collection_len, sizeof(uint64_t));

  if (!hashes) {
    return -1;
  }

  anu_file *file;
  bk_tree filetree = {0};

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);

    int hashing_ret = hash_video(file, config, &hashes[i * config->segments]);

    switch (hashing_ret) {
      case -1:
        {
          log_info("Failed to hash file %s", anu_file_get_filename(file));
          continue;
        }
      case -2:
        {
          /* We skipped this hash so lets move onto the next file. */
          continue;
        }
      default:
        {
          for (int j = 0; j < config->segments; j++) {
            bk_tree_insert(&filetree, hashes[(i * config->segments) + j], i);
          }
        }
    }
  }

  anu_report report = anu_generate_report(&files, hashes, config, &filetree);
  anu_print_report(&report, &files);
  anu_report_destroy(&report);
  bk_tree_node_free(filetree.root);
  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}

int main (int argc, char **argv) {

  anukrta_config config = {
      .dry_run = 0,
      .verbose = 0,
      .segments = 2,
      .threshold = 15,
      .hash_algorithm = ANU_HASH_ALGO_DCT,
      .skip_duration = anu_time_seconds_to_microseconds(1.0)};

  int parsing_return = anu_cli_parse_options(&config, argc, argv);

  if (parsing_return || config._exit_early) {
    exit(parsing_return);
  }
  if (config.verbose) {
    log_set_level(LOG_TRACE);
    anu_cli_print_configuration(&config);
  } else {
    log_set_level(LOG_WARN);
    av_log_set_level(AV_LOG_PANIC);
  }

  printf("\n--------------------\n");
  printf("Starting...\n");
  printf("--------------------\n");

  log_info("%s now running...", argv[0]);
  int driver_ret = anukrta_driver(&config);

  return driver_ret;
}
