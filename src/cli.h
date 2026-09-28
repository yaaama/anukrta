#ifndef AK_CLI_H
#define AK_CLI_H

#include <stdio.h>

#include "config.h"
#include "explore.h"

/**
 * What the CLI layer wants main() to do after applying the arguments.
 */
typedef enum ak_cli_action {
  AK_CLI_RUN = 0,   /**< Configuration is ready; proceed with the program. */
  AK_CLI_EXIT_OK,   /**< Early-exit command (--help/--version); exit 0. */
  AK_CLI_EXIT_FAIL, /**< Argument error; exit with `EXIT_FAILURE`. */
} ak_cli_action;

/**
 * Parse argv, mutating configuration struct passed in @p config.
 *
 * Positional arguments are collected into @p paths_out;
 * if none are given, `RT_SCAN_CURR_DIR` is set.
 * Malformed arguments are reported to `err`.
 *
 * --help/--version print and return AK_CLI_EXIT_OK immediately, so a
 * malformed option later on the command line cannot mask them.
 *
 * @return What main() should do next (see @ref ak_cli_action).
 */
ak_cli_action ak_cli_parse(int argc, char **argv, ak_config *config, ak_paths *paths, FILE *err);

/** Prints the resolved runtime configuration. */
void ak_cli_print_config(FILE *out, const ak_config *config);

#endif  // AK_CLI_H
