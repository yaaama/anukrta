#ifndef ANU_CLI_H
#define ANU_CLI_H

#include "util.h"

#define ANU_VERSION "0.0.1"

int anu_cli_parse_options(anukrta_config *config, int argc, char *const argv[]);
void anu_cli_print_configuration(anukrta_config *config);
#endif  // ANU_CLI_H
