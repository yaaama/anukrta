#include "cli.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define CLI_NAME "anukrta"

static const char *get_program_name (const char *arg_zero) {
  const char *last_slash = strrchr(arg_zero, '/');
  return last_slash ? last_slash + 1 : arg_zero;
}

static long get_available_threads (void) {
  errno = 0;
  long cores = sysconf(_SC_NPROCESSORS_ONLN);

  if (cores < 1 && errno) {
    perror("Error encountered whilst getting available cores.");
  }

  return MAXIMUM(cores, 1);
}

static void print_help (const char *program_name) {

  /* clang-format off */
  fprintf(stderr, "\nUsage: %s [OPTIONS...] [PATH]\n", program_name);
  fprintf(stderr, "\t-h, --help\t\tShow this help message\n");
  fprintf(stderr, "\t-v, --verbose\t\tEnable verbose output\n");
  fprintf(stderr, "\t-s, --segments\t\tNumber of segments to hash for each video\n");
  fprintf(stderr, "\t-t, --threshold\t\tMaximum distance threshold. Ranges from 0 to 64 (0 being the most similar).\n");
  fprintf(stderr, "\t--skip-duration\t\tVideos of this duration will be skipped.\n");
  fprintf(stderr, "\t--threads\t\tNumber of threads to use.\n");
  fprintf(stderr, "\t--version\t\tPrint version and exit.\n");
  fprintf(stderr, "\t--dry-run\t\tSimulate the run without making changes. \n");
  fprintf(stderr, "\t--detect-black\t\tDetect black frames and skip over them.\n");
  fprintf(stderr, "\t--detect-rotation\t\tDetect rotated videos.\n");
  /* clang-format on */
}

void anu_cli_print_configuration (anukrta_config *config) {

  printf("\n--- Configuration ---\n");
  printf("Verbose: %s\n", config->verbose ? "YES" : "NO");
  printf("Dry Run: %s\n", config->dry_run ? "YES" : "NO");
  printf("Detect Bars (Letterboxing | Windowboxing | Pillarboxing): %s\n",
         config->detect_bars ? "YES" : "NO");
  printf("Detect Black Frames: %s\n",
         config->detect_black_frames ? "YES" : "NO");
  printf("Detect Rotation: %s\n", config->detect_rotation ? "YES" : "NO");
  printf("Segments to hash: %zu\n", config->segments);
  printf("Maximum Distance Threshold: %zu\n", config->threshold);
  printf("Skip videos shorter than: %.1f seconds\n",
         (double) config->skip_duration / (double) ANU_TIME_ONE_SEC_IN_US);
  printf("Thread Count: %zu\n", config->thread_count);
}

/* Helper to reverse-lookup long option names */
static const char *get_long_opt_name (int val, const struct option *opts) {
  for (int i = 0; opts[i].name != NULL; i++) {
    if (opts[i].val == val) {
      return opts[i].name;
    }
  }
  return NULL;
}

/* Parses a string to a long, assigns out param (size_t) */
static int parse_numeric_arg_sizet (const char *restrict arg_name,
                                    const char *restrict arg_str,
                                    long min,
                                    long max,
                                    size_t *out) {
  char *endptr = NULL;
  errno = 0;

  long val = strtol(arg_str, &endptr, 10);

  if (errno == ERANGE || val < min || val > max) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME,
            arg_name, arg_str);
    if (max == LONG_MAX) {
      fprintf(stderr, "  Value must be %ld or greater.\n", min);
    } else {
      fprintf(stderr, "  Valid range is %ld to %ld.\n", min, max);
    }
    return -1;
  }

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "[%s] Error: %s requires a valid integer, got '%s'.\n",
            CLI_NAME, arg_name, arg_str);
    return -1;
  }

  *out = (size_t) val;
  return 0;
}

int anu_cli_parse_options (anukrta_config *config, int argc, char **argv) {

  const char *program_name = get_program_name(argv[0]);

  enum anu_options {  // NOLINT (*enum-initial-value)
    AUTO_HANDLE = 0,
    CMD_HELP = 'h',
    FLAG_VERBOSE = 'v',
    ARG_SEGMENTS = 's',
    ARG_THRESHOLD = 't',

    /* Auto-incrementing long-only options */
    CMD_VERSION = 1000,
    ARG_THREADS,
    ARG_SKIP_DURATION,
  };

  /* clang-format off */
  /* name, has_arg, flag, val */
  const struct option anukrta_opts[] = {
/*
 * COMMANDS:
 * These will cause the program to exit early.
 */
    {"help",            no_argument,       NULL,                         CMD_HELP},          // -h | --help
    {"version",         no_argument,       NULL,                         CMD_VERSION},       // --version
/*
 * RUNTIME CONFIG:
 * Options to customise how the program does things.
 */
    {"threshold",       required_argument, NULL,                         ARG_THRESHOLD},     // -t | --threshold
    {"segments",        required_argument, NULL,                         ARG_SEGMENTS},      // -s | --segments
    {"threads",         required_argument, NULL,                         ARG_THREADS},       // --threads
    {"skip-duration",   required_argument, NULL,                         ARG_SKIP_DURATION}, // --skip-duration
/*
 * FLAGS
 */
    /* TODO Let user specify verbosity (e.g '-vvvv' for trace granularity verbosity) */
    {"verbose",         no_argument,       NULL,                         FLAG_VERBOSE},      // -v | --verbose
    {"dry-run",         no_argument,       &config->dry_run,             1},                 // --dry-run
    {"detect-black",    no_argument,       &config->detect_black_frames, 1},                 // TODO
    {"detect-rotation", no_argument,       &config->detect_rotation,     1},                 // TODO

    {0,                 0,                 0,                            0}};                // END
  /* clang-format on */

  /* Short options */
  /* Start the opt string with ':' to take manual control of errors. */
  char *options_str = ":hvs:t:";

  int option_index = 0;
  int opt;

  int ret = 0;

  size_t available_threads = (size_t) get_available_threads();
  bool explicit_thread_count = false;

  while (1) {
    option_index = -1;

    // NOLINTBEGIN (concurrency-mt-unsafe)
    opt = getopt_long(argc, argv, options_str, anukrta_opts, &option_index);
    // NOLINTEND

    if (opt == -1) {
      break;
    }

    char arg_invoked[64];
    if (option_index != -1) {
      snprintf(arg_invoked, sizeof(arg_invoked), "--%s",
               anukrta_opts[option_index].name);
    } else {
      snprintf(arg_invoked, sizeof(arg_invoked), "-%c", opt);
    }

    switch (opt) {

      /* Matches for flags that are handled automatically (--dry-run for example) */
      case AUTO_HANDLE:
        {
          break;
        }

      /* When an option expects an argument but does not receive one */
      case ':':
        {
          const char *long_name = get_long_opt_name(optopt, anukrta_opts);

          if (long_name) {
            /* It was a long option (e.g., --threads) */
            fprintf(stderr, "%s: Option '--%s' requires an argument.\n",
                    program_name, long_name);
          } else {
            /* It was a short option (e.g., -t) */
            fprintf(stderr, "%s: Option '-%c' requires an argument.\n",
                    program_name, optopt);
          }

          fprintf(stderr, "Try '%s --help' for more information.\n",
                  program_name);
          goto exit_error;
        }
      case '?':
        {
          if (optopt) {
            const char *long_name = get_long_opt_name(optopt, anukrta_opts);
            if (long_name) {
              fprintf(stderr, "%s: Unrecognized option '--%s'.\n", program_name,
                      long_name);
            } else {
              fprintf(stderr, "%s: Unrecognized option '-%c'.\n", program_name,
                      optopt);
            }
          } else {
            /* optopt is sometimes 0 for unrecognized long options in certain libc implementations */
            fprintf(stderr, "%s: Unrecognized option.\n", program_name);
          }

          fprintf(stderr, "Try '%s --help' for more information.\n",
                  program_name);
          goto exit_error;
        }

      /* -h | --help */
      case CMD_HELP:
        {
          print_help(program_name);
          goto exit_early;
        }
      /* --version */
      case CMD_VERSION:
        {
          printf("%s - version: " ANU_VERSION "\n", program_name);
          goto exit_early;
          break;
        }

      /* -v | --verbose */
      case FLAG_VERBOSE:
        {
          config->verbose = 1;
          break;
        }

        /* -s | --segments */
      case ARG_SEGMENTS:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 1, 50,
                                      &config->segments) != 0) {
            goto exit_error;
          }
          break;
        }
      /* -t | --threshold */
      case ARG_THRESHOLD:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 0, 64,
                                      &config->threshold) != 0) {
            goto exit_error;
          }
          break;
        }

        /* --threads */
      case ARG_THREADS:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 1, LONG_MAX,
                                      &config->thread_count) != 0) {
            goto exit_error;
          }
          explicit_thread_count = true;
          if (config->thread_count > available_threads) {
            fprintf(stderr,
                    "%s: Ignoring option for threads (%d) since only '%d' "
                    "cores are available.\n",
                    CLI_NAME, config->thread_count, available_threads);
            config->thread_count = available_threads;
          }

          break;
        }
        /* TODO Implement this. */
      case ARG_SKIP_DURATION:
        {
          TODO("Not yet implemented skip duration.");
        }

      default:
        {
          UNREACHABLE(CLI_NAME ": Internal CLI Parsing Error");
        }
    }
  }

  /* Process remaining positional arguments */
  if (optind < argc) {
    int positional_arg_count = argc - optind;
    printf("\n--- Input Directories (%d) ---\n", positional_arg_count);

    config->paths_count = (size_t) positional_arg_count;
    config->paths = &argv[optind];

    for (int i = optind; i < argc; i++) {
      printf("  %s\n", argv[i]);
    }

  } else {
    printf("\n--- Scanning Current Directory ---\n");
    config->scan_curr_dir = 1;
    config->paths_count = 1;
    config->paths = NULL;
  }

  /* If thread is not explicitly stated, then assign default value (use all available threads) */
  if (!explicit_thread_count) {
    config->thread_count = (size_t) get_available_threads();
  }

  return ret;

exit_error:
  {
    config->_exit_early = 1;
    ret = 22;
    return ret;
  }

exit_early:
  {
    config->_exit_early = 1;
    return ret;
  }
}
