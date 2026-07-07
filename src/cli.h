#ifndef ANU_CLI_H
#define ANU_CLI_H

#include "config.h"

int anu_cli_parse_options(anu_config *config, int argc, char **argv);
void anu_cli_print_configuration(anu_config *config);
#endif  // ANU_CLI_H
