/* Video similarity tool */

#ifdef ANU_DEBUG
#    pragma message "Compilation in DEBUG mode."
#endif

#include <assert.h>
#include <inttypes.h>
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
#include "report.h"
#include "tree.h"
#include "util.h"
#include "vendor/log.h"
#include "video.h"

int anukrta_driver (anukrta_config *config, char *path) {

  /* Store the files we find in the path */
  anu_file_q files;
  /* Initialise the list to 20 items */
  anu_fileq_init(&files, 20);

  if (anu_recursive_filewalk(path, &files)) {
    log_warn("Encountered an error searching for files.");
    anu_fileq_destroy(&files);
    return -1;
  }
  size_t file_count = files.count;

  if (file_count < 1) {
    log_warn("No video files in '%s'", path);
    anu_fileq_destroy(&files);
    return -1;
  }

  log_info("Found `%zu` files (%s)", file_count, path);

  /* Array of hashes */
  size_t hash_collection_len = (file_count * config->segments);
  uint64_t *hashes = calloc(hash_collection_len, sizeof(uint64_t));

  if (!hashes) {
    exit(EXIT_FAILURE);
  }

  anu_file *file;
  bk_tree filetree = {0};

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);

    int hashing_ret = hash_video(file, config, &hashes[i * config->segments]);

    switch (hashing_ret) {
      case -1:
        {
          log_error("Failed to hash file %s", anu_file_get_filename(file));
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
  /* bk_tree_print_ascii(&filetree); */
  bk_tree_node_free(filetree.root);
  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}

int main (int argc, char **argv) {

  anukrta_config config = {
      .simulate = 0,
      .verbose = 0,
      .segments = 2,
      .threshold = 15,
      .hash_algorithm = ANU_HASH_ALGO_DCT,
      .skip_duration = anu_time_seconds_to_microseconds(1.0)};

  int parsing_return = anu_cli_parse_options(&config, argc, argv);

  if (parsing_return == -1) {
    exit(EXIT_FAILURE);
  } else if (parsing_return == EXIT_SUCCESS) {
    exit(EXIT_SUCCESS);
  }

  if ((argc - parsing_return) > 1) {
    printf("We only handle a single directory for now.\n");
    exit(EXIT_FAILURE);
  }

  char *path = argv[parsing_return];

  if (config.verbose) {
    log_set_level(LOG_TRACE);
    anu_cli_print_configuration(&config);
  } else {
    log_set_level(LOG_DEBUG);
  }

  printf("\n--------------------\n");
  printf("Starting...\n");
  printf("--------------------\n");

  log_info("%s now running...", argv[0]);
  anukrta_driver(&config, path);

  return 0;
}
