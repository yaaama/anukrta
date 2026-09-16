#ifndef AK_CLI_H
#define AK_CLI_H

#include "config.h"
#include "explore.h"

int ak_cli_parse_args(ak_config *config, int argc, char **argv, ak_paths *paths_out);
void ak_cli_print_config(ak_config *config);
#endif  // AK_CLI_H
