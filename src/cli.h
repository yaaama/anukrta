#ifndef ANU_CLI_H
#define ANU_CLI_H

#include "config.h"
#include "explore.h"

int anu_cli_parse_options(anu_config *config, int argc, char **argv, anu_paths *paths_out);
void anu_cli_print_configuration(anu_config *config);
#endif  // ANU_CLI_H
