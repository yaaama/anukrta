#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/redirect.h>
#include <errno.h>
#include <getopt.h>

#include "../src/cli.h"
#include "config.h"

static void reset_optind (void) { optind = 1; }

static void setup (void) {
  /* Reset option index */
  reset_optind();
  /* Redirect standard output and err from tests */
  cr_redirect_stdout();
  cr_redirect_stderr();
}

TestSuite(CLI, .init = setup, .description = "CLI Related Unit Tests");

/* Testing without any options (default behaviour) */
Test (CLI,
      general_args_none,
      .description = "Test for when no arguments are passed") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", NULL};
  int argc = 1;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(eq(config.runtime_flags & RT_SCAN_CURR_DIR, RT_SCAN_CURR_DIR));
  cr_assert(zero(config.runtime_flags & RT_VERBOSE));
}

Test (CLI,
      general_args_positional,
      .description = "Test positional argument parsing") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "/path/to/vids", "/other/path", NULL};
  int argc = 3;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(config.paths_count == 2);
  cr_assert(eq(str, config.paths[0], "/path/to/vids"));
  cr_assert(eq(str, config.paths[1], "/other/path"));

  cr_assert(zero(config.runtime_flags & RT_SCAN_CURR_DIR));
}

Test (CLI,
      general_args_numeric,
      .description = "Test parsing of numerical options (short & long)") {
  anukrta_config config = {0};
  int argc = 5;
  int ret;

  /* SHORT */
  char segment_c[] = "15";
  char thread_c[] = "30";
  char *argv_short[] = {"anukrta", "-s", segment_c, "-t", thread_c, NULL};

  ret = anu_cli_parse_options(&config, argc, argv_short);
  cr_assert(ret == 0);
  cr_assert(config.segments == 15);
  cr_assert(config.threshold == 30);

  /* LONG */
  reset_optind();
  char thread_c_long[] = "10";
  char segment_c_long[] = "8";
  char *argv_long[] = {"anukrta",    "--threads",    thread_c_long,
                       "--segments", segment_c_long, NULL};

  ret = anu_cli_parse_options(&config, argc, argv_long);

  cr_assert(ret == 0);
  cr_assert(config.segments == 8);
  cr_assert(config.thread_count == 10);
}

/* --help */
Test (CLI, help_flag_long, .description = "Parsing help flags (short & long)") {
  anukrta_config config = {0};
  int argc = 2;
  int ret;

  /* SHORT */
  char *argv_short[] = {"anukrta", "-h", NULL};
  ret = anu_cli_parse_options(&config, argc, argv_short);
  cr_assert(ret == 0);

  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));

  /* LONG */
  reset_optind();
  char *argv_long[] = {"anukrta", "--help", NULL};
  ret = anu_cli_parse_options(&config, argc, argv_long);

  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));
}

/* '--verbose' */
Test (CLI,
      verbose_flag_long,
      .description = "Parsing verbose flags (short & long)") {
  anukrta_config config = {0};
  char *verbose_long[] = {"anukrta", "--verbose", NULL};
  int verbose_long_c = 2;
  int ret = anu_cli_parse_options(&config, verbose_long_c, verbose_long);

  cr_assert(ret == 0);

  cr_assert(gt(config.runtime_flags & RT_VERBOSE, 0));

  char *verbose_short[] = {"anukrta", "-v", NULL};
  int verbose_short_c = 2;
  ret = anu_cli_parse_options(&config, verbose_short_c, verbose_short);
  cr_assert(ret == 0);
  cr_assert(gt(config.runtime_flags & RT_VERBOSE, 0));
}

/* --dry-run */
Test (CLI, dryrun_flag, .description = "Parsing '--dry-run' flag") {
  anukrta_config config = {0};
  /* Note: dry-run uses the 'flag' pointer in struct option */
  char *argv[] = {"anukrta", "--dry-run", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);

  cr_assert(gt(config.runtime_flags & RT_DRY_RUN, 0));
}

/* --version */
Test (CLI, version_exit_early, .description = "Parsing '--version' flag") {
  anukrta_config config = {0};
  char *argv[] = {"anukrta", "--version", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 0);
  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));
}

/* Invalid -s value */
Test (CLI,
      segments_short_invalid,
      .description = "Pass invalid value for segments option") {
  anukrta_config config = {0};
  /* Segments max is 50 */
  char *argv_short[] = {"anukrta", "-s", "999", NULL};
  char *argv_long[] = {"anukrta", "--segments", "999", NULL};
  int argc = 3;

  int ret = anu_cli_parse_options(&config, argc, argv_short);

  cr_assert(ret == EINVAL);

  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));

  reset_optind();
  ret = anu_cli_parse_options(&config, argc, argv_long);

  cr_assert(ret == EINVAL);

  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));
}

/* Missing arg for '-t' */
Test (CLI,
      threads_args_none,
      .description = "Missing argument for option '-t'") {
  anukrta_config config = {0};
  /* -t requires an argument */
  char *argv[] = {"anukrta", "-t", NULL};
  int argc = 2;

  int ret = anu_cli_parse_options(&config, argc, argv);

  cr_assert(ret == 22);
  cr_assert(gt(config.runtime_flags & RT_EXIT_EARLY, 0));
}
