#ifndef AK_CLI_H
#define AK_CLI_H

#include <stdio.h>

#include "config.h"
#include "explore.h"
#include "kvec.h"
#include "util.h"

/**
 * @brief What the CLI layer wants main() to do after applying the arguments.
 */
typedef enum ak_cli_action {
  AK_CLI_RUN = 0,   /**< Configuration is ready; proceed with the program. */
  AK_CLI_EXIT_OK,   /**< Early-exit command (--help/--version); exit 0. */
  AK_CLI_EXIT_FAIL, /**< Argument error; exit with EXIT_FAILURE. */
} ak_cli_action;

/**
 * @brief A single option occurrence parsed from argv.
 * Strings are borrowed from argv and must not be freed.
 */
typedef struct ak_cli_event {
  const char *arg; /**< Option argument, or NULL if the option took none. */
  int opt_id;      /**< One of @ref ak_cli_opt_id. */
  int def_index;   /* internal: row in cli_opt_defs[] */
} ak_cli_event;

typedef kvec_t(ak_cli_event) ak_cli_events;

AK_DEFINE_AUTO(cli_events, ak_cli_events, kv_destroy(*ak__obj))

/**
 * @brief Phase 1: turn argv into a list of option events.
 * @details The tokenizer has no side effects beyond filling `events_out`
 * (all strings are borrowed from argv). Malformed arguments are reported to
 * `err` and cause a -1 return.
 *
 * @retval 0 on success.
 * @retval -1 if the argument vector is malformed.
 */
int ak_cli_tokenize(int argc, char **argv, ak_cli_events *events_out, FILE *err);

/**
 * @brief Phase 2: apply option events to the configuration.
 * @details Handles early-exit commands (--help/--version) and value
 * validation. Positional events are collected into `paths_out`; if none are
 * given, RT_SCAN_CURR_DIR is set on the config.
 *
 * @return What main() should do next (see @ref ak_cli_action).
 */
ak_cli_action ak_cli_apply(ak_config *config,
                           const ak_cli_events *events,
                           const char *program_name,
                           ak_paths *paths_out);

/** Prints the resolved runtime configuration. */
void ak_cli_print_config(FILE *out, const ak_config *config);

#endif  // AK_CLI_H
