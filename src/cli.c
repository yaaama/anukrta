#include "cli.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const char *get_program_name (const char *arg_zero) {
  const char *last_slash = strrchr(arg_zero, '/');
  return last_slash ? last_slash + 1 : arg_zero;
}

static void print_help (const char *program_name) {

  fprintf(stderr, "Usage: %s [OPTIONS...] [PATH]\n", program_name);
  fprintf(stderr, "\t-h, --help\t\tShow this help message\n");
  fprintf(stderr, "\t-v, --verbose\t\tEnable verbose output\n");
  fprintf(stderr,
          "\t-s, --segments\t\tNumber of segments to hash for each video\n");
  fprintf(stderr,
          "\t-t, --threshold\t\tMaximum distance threshold. Ranges from 0 to "
          "64 (0 being the most similar).\n");

  fprintf(stderr,
          "\t--skip-duration\t\tVideos of this duration will be skipped.\n");
  fprintf(stderr, "\t--threads\t\tNumber of threads to use.\n");
  fprintf(stderr, "\t--version\t\tPrint version and exit\n");
  fprintf(stderr, "\t--dry-run\t\tSimulate the run without making changes\n");
  fprintf(stderr,
          "\t--detect-black\t\tDetect black frames and skip over them "
          "(NOOP)\n");
  fprintf(stderr, "\t--detect-rotation\t\tDetect rotated videos\n");
}

void anu_cli_print_configuration (anukrta_config *config) {

  printf("\n--- Configuration ---\n");
  printf("Verbose: %s\n", config->verbose ? "YES" : "NO");
  printf("Dry Run: %s\n", config->dry_run ? "YES" : "NO");
  printf("Detect Black Frames: %s\n",
         config->detect_black_frames ? "YES" : "NO");
  printf("Detect Rotation: %s\n", config->detect_rotation ? "YES" : "NO");
  printf("Segments to hash: %zu\n", config->segments);
  printf("Maximum Distance Threshold: %zu\n", config->threshold);
  printf("Skip videos shorter than: %.1f seconds\n",
         (double) config->skip_duration / (double) ANU_TIME_ONE_SEC_IN_US);
  printf("Thread Count: %zu\n", config->thread_count);
}

int validate_threads_value (char *arg_str, size_t *out) {

  char *endptr = NULL;
  /* Reset errno to 0 before calling strtol */
  errno = 0;

  /* Convert string (arg_str) to a long integer in base 10 */
  long val = strtol(arg_str, &endptr, 10);

  int parse_err = errno;

  /* Check for overflow/underflow */
  if (parse_err == ERANGE) {
    fprintf(stderr, "Error: --threads value '%s' is out of range.\n", arg_str);
    return -1;
  }

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "Error: --threads requires a valid integer, got '%s'.\n",
            arg_str);
    return -1;
  }

  if (val < 0) {
    fprintf(stderr, "Error: --threads must be 1 or greater.\n");
    return -1;
  }

  *out = (size_t) val;
  return 0;
}

int validate_segments_value (char *arg_str, size_t *out) {

  char *endptr = NULL;
  errno = 0;

  /* Convert string (arg_str) to a long integer in base 10 */
  long val = strtol(arg_str, &endptr, 10);

  int parse_err = errno;
  /* Check for overflow/underflow */
  if (parse_err == ERANGE) {
    fprintf(stderr,
            "Error: --segments value '%s' is out of range. A sensible value "
            "ranges between 2 to 10.\n",
            arg_str);
    return -1;
  }

  /* Check if the user passed non-numeric gibberish.
   * If endptr == arg_str, they passed something like "abc"
   * If *endptr != '\0', they passed a mix like "5abc" */
  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "Error: --segments requires a valid integer, got '%s'.\n",
            arg_str);
    return -1;
  }

  /* logic validation (segments shouldn't be negative or 0) */
  if (val <= 0) {
    fprintf(stderr, "Error: --segments must be 1 or greater.\n");
    return -1;
  }

  if (val > 50) {
    fprintf(stderr, "Error: Law of diminishing returns.\n");
    return -1;
  }

  *out = (size_t) val;
  return 0;
}

int validate_threshold_value (char *arg_str, size_t *out) {

  char *endptr = NULL;
  errno = 0;

  /* Convert string to a long integer in base 10 */
  long val = strtol(arg_str, &endptr, 10);
  int parse_err = errno;

  /* Check for overflow/underflow */
  if (parse_err == ERANGE) {
    fprintf(stderr,
            "Error: --threshold value '%s' is out of range. Threshold value "
            "should range from 0 to 64 (0 being exact duplicates).\n",
            arg_str);
    return -1;
  }

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "Error: --threshold requires a valid integer, got '%s'.\n",
            arg_str);
    return -1;
  }

  /* logic validation, similarity threshold shouldn't be negative or above 64 */
  if (val < 0 || val > 64) {
    fprintf(stderr, "Error: --threshold must range between 0 and 64.\n");
    return -1;
  }

  *out = (size_t) val;
  return 0;
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

  /* name, has_arg, flag, val */
  const struct option anukrta_opts[] = {
    /* # Commands */
    {"help", no_argument, NULL, CMD_HELP},       /* -h | --help */
    {"version", no_argument, NULL, CMD_VERSION}, /* --version */

    /* # Configuration */
    /* --skip-duration */
    {"skip-duration", required_argument, NULL, ARG_SKIP_DURATION},
    /* -t | --threshold */
    {"threshold", required_argument, NULL, ARG_THRESHOLD},
    /* --threads */
    {"threads", required_argument, NULL, ARG_THREADS},
    /* -s | --segments */
    {"segments", required_argument, NULL, ARG_SEGMENTS},

    /* # Flags (Automatically Handled) */
    {"verbose", no_argument, &config->verbose, 1}, /* -v | --verbose */
    {"dry-run", no_argument, &config->dry_run, 1}, /* --dry-run */
    {"detect-black", no_argument, &config->detect_black_frames, 1},
    {"detect-rotation", no_argument, &config->detect_rotation, 1},
    {0, 0, 0, 0}};

  /* Short options */
  /* Start the opt string with ':' to take manual control of errors. */
  char *options_str = ":hvs:t:";

  int option_index = 0;
  int opt;

  while (1) {
    // NOLINTBEGIN (concurrency-mt-unsafe)
    opt = getopt_long(argc, argv, options_str, anukrta_opts, &option_index);
    // NOLINTEND

    if (opt == -1) {
      break;
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
          config->_exit_early = 1;
          return -1;
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
          config->_exit_early = 1;
          return -1;
        }

      /* -h | --help */
      case CMD_HELP:
        {
          print_help(program_name);
          config->_exit_early = 1;
          return 0;
        }
      /* --version */
      case CMD_VERSION:
        {
          printf("%s - version: " ANU_VERSION "\n", program_name);
          config->_exit_early = 1;
          return 0;
        }

      /* -s | --segments */
      case ARG_SEGMENTS:
        {
          size_t val = 0;
          if (validate_segments_value(optarg, &val)) {
            config->_exit_early = 1;
            return -1;
          };
          assert(val > 0);
          config->segments = val;
          break;
        }

      /* -t | --threshold */
      case ARG_THRESHOLD:
        {
          size_t val = 0;
          if (validate_threshold_value(optarg, &val)) {

            config->_exit_early = 1;
            return -1;
          };
          config->threshold = val;
          break;
        }
      /* --threads */
      case ARG_THREADS:
        {
          size_t val = 0;
          if (validate_threads_value(optarg, &val)) {
            config->_exit_early = 1;
            return -1;
          };
          config->thread_count = val;
          break;
        }

      /* TODO Let user specify verbosity (e.g '-vvvv' for trace granularity verbosity) */
      case FLAG_VERBOSE: /* -v | --verbose */
        {
          config->verbose = 1;
          break;
        }

        /* TODO Implement this. */
      case ARG_SKIP_DURATION:
        {
          TODO("Not yet implemented skip duration.");
        }

      default:
        {
          UNREACHABLE("anukrta: Internal CLI Parsing Error");
        }
    }
  }

  /* Process remaining positional arguments */
  int temp_optind = optind;

  if (optind < argc) {
    assert((argc - optind) > 0);
    printf("\n--- Input Directories (%d) ---\n", argc - optind);
    config->paths_count = (size_t) (argc - optind);
    config->paths = &argv[optind];
  } else {

    printf("\n--- Scanning Current Directory ---\n");
    config->scan_curr_dir = 1;
    config->paths_count = 1;
    config->paths = NULL;
  }
  while (temp_optind < argc) {
    printf("%s\n", argv[temp_optind]);
    ++temp_optind;
  }
  return 0;
}
