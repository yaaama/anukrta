#include "cli.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

#define CLI_NAME "anukrta"
#define ANU_VERSION "0.0.1"

/* TODO: Remove this later... */
MAYBE_UNUSED static const char *get_program_name (const char *arg_zero) {
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

static void print_help (void) {
  const int OPT_W = 30;
#define PRINT_HEADING(text) fprintf(stderr, "\n%s:\n", text)
#define PRINT_OPT(opt, desc) fprintf(stderr, "  %-*s %s\n", OPT_W, opt, desc)

  /* clang-format off */
  fprintf(stderr, "\nUsage: " CLI_NAME " [OPTIONS...] [PATH]\n\n");

  PRINT_HEADING("General Options");
  PRINT_OPT("-h, --help", "Show this help message.");
  PRINT_OPT("--version", "Print version and exit.");
  PRINT_OPT("-v, --verbose", "Enable verbose output.");
  PRINT_OPT("--dry-run", "Simulate the run without making changes.");

  PRINT_HEADING("Algorithm & Tuning");
  PRINT_OPT("-s, --segments=integer", "Number of segments to hash for each video (default: 3).");
  PRINT_OPT("-t, --threshold=integer", "Maximum distance threshold (default 15).");
  PRINT_OPT("", "Ranges from 0 to 64 (0 being the most similar).");
  PRINT_OPT("--skip-duration=integer", "Skip videos shorter than N seconds (default 3).");

  PRINT_HEADING("Detection");
  PRINT_OPT("--detect-black=bool", "Detect black frames and skip over them (default: yes).");
  PRINT_OPT("--detect-bars=bool", "Detect bars around video (e.g. letterboxing) (default: yes).");
  PRINT_OPT("--detect-rotation=bool", "Detect rotated videos (default: yes).");

  PRINT_HEADING("Execution & Storage");
  PRINT_OPT("--threads=integer", "Number of threads to use (default ALL).");
  PRINT_OPT("--cache=bool", "Results should be stored in cache (default: yes).");

  fprintf(stderr, "\n");
  /* clang-format on */

#undef PRINT_HEADING
#undef PRINT_OPT
}

void anu_cli_print_configuration (anukrta_config *config) {

  const int OPT_W = 30;

#define PRINT_HEADING(text) fprintf(stdout, "\n----- %s -----\n", text)
#define PRINT_CONFIG_STR(cfg, val) \
  fprintf(stdout, "  %-*s : %s\n", OPT_W, cfg, val)
#define PRINT_CONFIG_ZU(cfg, val) \
  fprintf(stdout, "  %-*s : %zu\n", OPT_W, cfg, val)
#define FLAG_VAL(var, flag) (ANU_HAS_ANY_FLAG((var), (flag)) ? "YES" : "NO")

  /* clang-format off */
  flags32 rtflags = config->runtime_flags;
  flags32 detflags = config->detect_flags;
  flags32 reportflags = config->report_flags;

  fprintf(stdout, "\n===== Runtime Configuration =====\n");
  PRINT_HEADING("General");
  PRINT_CONFIG_STR("Verbose", FLAG_VAL(rtflags, RT_VERBOSE));
  PRINT_CONFIG_STR("Dry Run", FLAG_VAL(rtflags, RT_DRY_RUN));
  PRINT_CONFIG_STR("Scan Current Directory", FLAG_VAL(rtflags, RT_SCAN_CURR_DIR));
  PRINT_CONFIG_STR("Cache Results", FLAG_VAL(rtflags, RT_CACHE));

    PRINT_HEADING("Algorithm Settings");
  PRINT_CONFIG_ZU("Segments to hash", config->segments);
  PRINT_CONFIG_ZU("Maximum Distance Threshold", config->threshold);
  PRINT_CONFIG_ZU("Skip videos shorter than", config->skip_duration);
  PRINT_CONFIG_ZU("Thread Count", config->thread_count);

  PRINT_HEADING("Report Flags");
  PRINT_CONFIG_STR("Print Hashes in Report", FLAG_VAL(reportflags, REPORT_PRINT_HASHES));

  PRINT_HEADING("Detection Flags");
  PRINT_CONFIG_STR("Detect Bars", FLAG_VAL(detflags, DETECT_BARS));
  PRINT_CONFIG_STR("Detect Black Frames", FLAG_VAL(detflags, DETECT_BLACK_FRAME));
  PRINT_CONFIG_STR("Detect Rotatation", FLAG_VAL(detflags, DETECT_ROTATION));
  /* clang-format on */
  fprintf(stdout, "\n");
#undef PRINT_HEADING
#undef PRINT_CONFIG_STR
#undef PRINT_CONFIG_ZU
#undef FLAG_VAL
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
[[maybe_unused]] static int parse_arg_integer (const char *restrict arg_name,
                                               const char *restrict arg_str,
                                               int min,
                                               int max,
                                               int *out) {

  if (!arg_name || !arg_str || !out) {
    return -1;
  }

  char *endptr = NULL;
  errno = 0;

  long val = strtol(arg_str, &endptr, 10);

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "[%s] Error: %s requires a valid integer, got '%s'.\n",
            CLI_NAME, arg_name, arg_str);
    return -1;
  }

  if (errno == ERANGE || val < min || val > max || val > INT_MAX ||
      val < INT_MIN) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME,
            arg_name, arg_str);

    if (max == INT_MAX && min == INT_MIN) {
      // Both are unbounded (fits in any int)
      fprintf(stderr, "  Value must fit within a standard integer.\n");
    } else if (max == INT_MAX) {
      fprintf(stderr, "  Value must be %d or greater.\n", min);
    } else if (min == INT_MIN) {
      fprintf(stderr, "  Value must be %d or less.\n", max);
    } else {
      fprintf(stderr, "  Valid range is %d to %d.\n", min, max);
    }
    return -1;
  }

  *out = (int) val;
  return 0;
}

/** Parses a string to a size_t, putting value into *out param */
static int parse_numeric_arg_sizet (const char *restrict arg_name,
                                    const char *restrict arg_str,
                                    size_t min,
                                    size_t max,
                                    size_t *out) {
  if (!arg_name || !arg_str || !out) {
    return -1;
  }

  /* Prevent negatives from being parsed */
  const char *p = arg_str;
  /* Skip leading whitespace */
  while (isspace((unsigned char) *p)) {
    ++p;
  }

  if (*p == '-') {
    fprintf(stderr, "[%s] Error: %s cannot be negative.\n", CLI_NAME, arg_name);
    return -1;
  }

  char *endptr = NULL;
  errno = 0;

  unsigned long val = strtoul(arg_str, &endptr, 10);

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr,
            "[%s] Error: %s requires a valid positive integer, got '%s'.\n",
            CLI_NAME, arg_name, arg_str);
    return -1;
  }

  if (errno == ERANGE || val < min || val > max || val > ULONG_MAX) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME,
            arg_name, arg_str);
    if (max == LONG_MAX) {
      fprintf(stderr, "  Value must be %zu or greater.\n", min);
    } else {
      fprintf(stderr, "  Valid range is %zu to %zu.\n", min, max);
    }
    return -1;
  }

  *out = val;
  return 0;
}

/**
 * @brief Parses a string into a boolean (1 or 0).
 * @retval -1 if the string is not a recognized boolean value.
 * @retval 0 if false.
 * @retval 1 if true.
 */
static int parse_bool_arg (const char *restrict arg_name,
                           const char *restrict arg_str) {
  if (!arg_str) {
    return -1;
  }

  assert(arg_name);

  if (strcmp(arg_str, "1") == 0 || strcasecmp(arg_str, "yes") == 0 ||
      strcasecmp(arg_str, "true") == 0) {
    return 1;
  }
  if (strcmp(arg_str, "0") == 0 || strcasecmp(arg_str, "no") == 0 ||
      strcasecmp(arg_str, "false") == 0) {
    return 0;
  }

  fprintf(stderr, "[%s] Error: Invalid argument for boolean option '%s'\n",
          CLI_NAME, arg_name);
  return -1;
}

int anu_cli_parse_options (anukrta_config *config, int argc, char **argv) {

  const char *program_name = CLI_NAME;

  enum anu_options {  // NOLINT (*enum-initial-value)
    AUTO_HANDLE = 0,
    CMD_HELP = 'h',
    ARG_SEGMENTS = 's',
    ARG_THRESHOLD = 't',
    FLAG_VERBOSE = 'v',

    /* Auto-incrementing long-only options */
    CMD_VERSION = 256,
    ARG_THREADS,
    ARG_SKIP_DURATION,
    FLAG_DRY_RUN,
    FLAG_CACHE,
    FLAG_DETECT_BLACK_FRAME,
    FLAG_DETECT_BARS,
    FLAG_DETECT_ROTATION,
  };

  /* clang-format off */


  /* name, has_arg, flag, val */
  const struct option anukrta_opts[] = {

  /* NOTE: Realigning struct in Emacs:
     prefix-arg + align-regexp --> ,\(\s-*\) --> 1 --> 4 --> yes
   */
/*
 * COMMANDS:
 * These will cause the program to exit early.
 */
    {"help",               no_argument,          NULL,                CMD_HELP},                   // -h | --help
    {"version",            no_argument,          NULL,                CMD_VERSION},                // --version
/*
 * RUNTIME CONFIG:
 * Options to customise how the program does things.
 */
    {"threshold",          required_argument,    NULL,                ARG_THRESHOLD},              // -t | --threshold
    {"segments",           required_argument,    NULL,                ARG_SEGMENTS},               // -s | --segments
    {"threads",            required_argument,    NULL,                ARG_THREADS},                // --threads
    {"skip-duration",      required_argument,    NULL,                ARG_SKIP_DURATION},          // --skip-duration
/*
 * FLAGS
 */
    /* TODO: Let user specify verbosity (e.g '-vvvv' for trace granularity verbosity) */
    {"verbose",            no_argument,          NULL,                FLAG_VERBOSE},               // -v | --verbose
    {"dry-run",            no_argument,          NULL,                FLAG_DRY_RUN},               // --dry-run
    {"detect-black",       optional_argument,    NULL,                FLAG_DETECT_BLACK_FRAME},    // --detect-black
    {"detect-rotation",    optional_argument,    NULL,                FLAG_DETECT_ROTATION},       // --detect-rotation
    {"detect-bars",        optional_argument,    NULL,                FLAG_DETECT_BARS},           // --detect-bars
    {"cache",        optional_argument,    NULL,                FLAG_CACHE},           // --cache

    {0,                    0,                    0,                   0}};                // END
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
          print_help();
          ANU_SET_FLAG(config->runtime_flags, RT_EXIT_EARLY);
          return 0;
        }
      /* --version */
      case CMD_VERSION:
        {
          printf("%s - version: " ANU_VERSION "\n", program_name);
          ANU_SET_FLAG(config->runtime_flags, RT_EXIT_EARLY);
          return 0;
        }

      /* -v | --verbose */
      case FLAG_VERBOSE:
        {
          ANU_SET_FLAG(config->runtime_flags, RT_VERBOSE);
          break;
        }

      /* --dry-run */
      case FLAG_DRY_RUN:
        {
          ANU_SET_FLAG(config->runtime_flags, RT_DRY_RUN);
          break;
        }

      /* --cache */
      case FLAG_CACHE:
        {
          /* No arg after opt */
          if (optarg == NULL) {
            ANU_SET_FLAG(config->runtime_flags, RT_CACHE);
          }

          int cache_results_bool = parse_bool_arg(arg_invoked, optarg);
          if (cache_results_bool == -1) {
            goto exit_error;
          }
          if (cache_results_bool) {
            ANU_SET_FLAG(config->runtime_flags, RT_CACHE);
          } else {
            ANU_CLEAR_FLAG(config->runtime_flags, RT_CACHE);
          }

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
                    "%s: Ignoring option for threads (%zu) since only %zu "
                    "cores are available.\n",
                    CLI_NAME, config->thread_count, available_threads);
            config->thread_count = available_threads;
          }

          break;
        }
      case ARG_SKIP_DURATION: /* --skip-duration */
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 0, INT_MAX,
                                      &config->skip_duration) != 0) {
            goto exit_error;
          }
          break;
        }
      case FLAG_DETECT_BARS: /* --detect-bars */
        {
          /* No arg after opt */
          if (optarg == NULL) {
            ANU_SET_FLAG(config->detect_flags, DETECT_BARS);
          }
          int detect_bars_arg = parse_bool_arg(arg_invoked, optarg);

          if (detect_bars_arg == -1) {
            goto exit_error;
          }
          if (detect_bars_arg) {
            ANU_SET_FLAG(config->detect_flags, DETECT_BARS);
          } else {
            ANU_CLEAR_FLAG(config->detect_flags, DETECT_BARS);
          }

          break;
        }
      case FLAG_DETECT_BLACK_FRAME: /* --detect-black */
        {
          /* No arg after opt */
          if (optarg == NULL) {
            ANU_SET_FLAG(config->detect_flags, DETECT_BLACK_FRAME);
          }
          int detect_blackframe_arg = parse_bool_arg(arg_invoked, optarg);

          if (detect_blackframe_arg == -1) {
            goto exit_error;
          }
          if (detect_blackframe_arg) {
            ANU_SET_FLAG(config->detect_flags, DETECT_BLACK_FRAME);
          } else {
            ANU_CLEAR_FLAG(config->detect_flags, DETECT_BLACK_FRAME);
          }
          break;
        }

      case FLAG_DETECT_ROTATION:
        {  // --detect-black
          if (optarg == NULL) {
            ANU_SET_FLAG(config->detect_flags,
                         DETECT_ROTATION);  // Just --detect-blackframe
          }
          int detect_rotation_arg = parse_bool_arg(arg_invoked, optarg);

          if (detect_rotation_arg == -1) {
            goto exit_error;
          }
          if (detect_rotation_arg == 0) {
            ANU_CLEAR_FLAG(config->detect_flags, DETECT_ROTATION);
          } else {
            ANU_SET_FLAG(config->detect_flags, DETECT_ROTATION);
          }
          break;
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
    ANU_SET_FLAG(config->runtime_flags, RT_SCAN_CURR_DIR);
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
    ANU_SET_FLAG(config->runtime_flags, RT_EXIT_EARLY);
    ret = 22;
    return ret;
  }
}
