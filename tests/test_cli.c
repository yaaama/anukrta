#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/redirect.h>
#include <errno.h>
#include <getopt.h>

#include "../src/cli.h"

static void setup (void) {
  /* Reset option index */
  optind = 1;
  /* Redirect standard output and err from tests */
  cr_redirect_stdout();
  cr_redirect_stderr();
}

TestSuite(cli, .init = setup, .description = "CLI Related Unit Tests");

/* Testing without any options (default behaviour) */
Test (cli, general_args_none) {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", NULL};
  int argc = 1;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.scan_curr_dir == 1, "Should scan current dir by default");
  cr_assert(config.verbose == 0);
}

Test (cli, general_args_positional) {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "/path/to/vids", "/other/path", NULL};
  int argc = 3;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.paths_count == 2);
  cr_assert(eq(str, config.paths[0], "/path/to/vids"));
  cr_assert(eq(str, config.paths[1], "/other/path"));
  cr_assert(config.scan_curr_dir == 0);
}

Test (cli, general_args_numeric_short) {
  char segment_c[] = "15";
  char thread_c[] = "30";
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "-s", segment_c, "-t", thread_c, NULL};
  int argc = 5;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.segments == 15);
  cr_assert(config.threshold == 30);
}

Test (cli, general_args_numeric_long) {
  anukrta_config config = {0};
  char segment_c[] = "8";
  char thread_c[] = "10";
  char *argv[] = {"anukrta",    "--threads", thread_c,
                  "--segments", segment_c,   NULL};
  int argc = 5;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.segments == 8);
  cr_assert(config.thread_count == 10);
}

/* -h */
Test (cli, help_flag_short, .description = "Parsing '-h'") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "-h", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config._exit_early == 1);
}

/* --help */
Test (cli, help_flag_long, .description = "Parsing '--help'") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "--help", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config._exit_early == 1);
}

/* '-v' */
Test (cli, verbose_flag_short, .description = "Parsing '-v'") {

  anukrta_config config = {0};
  char *verbose_short[] = {"anukrta", "-v", NULL};
  int verbose_short_c = 2;
  int ret = anu_cli_parse_options(&config, verbose_short_c, verbose_short);
  cr_assert(ret == 0);
  cr_assert(config.verbose == 1);
}

/* '--verbose' */
Test (cli, verbose_flag_long, .description = "Parsing '--verbose'") {
  anukrta_config config = {0};
  char *verbose_long[] = {"anukrta", "--verbose", NULL};
  int verbose_long_c = 2;
  int ret = anu_cli_parse_options(&config, verbose_long_c, verbose_long);

  cr_assert(ret == 0);
  cr_assert(config.verbose == 1);
}

/* --dry-run */
Test (cli, dryrun_flag, .description = "Parsing '--dry-run'") {
  anukrta_config config = {0};
  /* Note: dry-run uses the 'flag' pointer in struct option */
  char *argv[] = {"anukrta", "--dry-run", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.dry_run == 1);
}

/* --version */
Test (cli, version_exit_early, .description = "Parsing '--version'") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "--version", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config._exit_early == 1);
}

/* Invalid -s value */
Test (cli, segments_short_invalid, .description = "Invalid segments argument") {
  anukrta_config config = {0};
  /* Segments max is 50 */
  char *argv[] = {"anukrta", "-s", "999", NULL};
  int argc = 3;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == EINVAL);
  cr_assert(config._exit_early == 1);
}

/* Missing arg for '-t' */
Test (cli, threads_args_none, .description = "Missing value for '-t'") {
  anukrta_config config = {0};
  /* -t requires an argument */
  char *argv[] = {"anukrta", "-t", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 22);
  cr_assert(config._exit_early == 1);
}
