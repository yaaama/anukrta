#include "cli.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>  // IWYU pragma: keep
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "kvec.h"
#include "util.h"

#define CLI_NAME "anukrta"
#define AK_VERSION "0.0.1"

static long get_available_threads (void) {
  errno = 0;
  long cores = sysconf(_SC_NPROCESSORS_ONLN);

  if (cores < 1) {
    if (errno != 0) {
      perror("Failed to retrieve core-count: ");
    } else {
      fprintf(stderr, "Could not determine core-count.\n");
    }
  }
  return MAXIMUM(cores, 1);
}

static void print_help (void) {
  const int OPT_W = 30;

  static const char *example =
      "anukrta --cache=no --verbose --segments=5 /dir/one/ /dir/two/ "
      "videoFile.mp4";

  ak_config cfg = anukrta_default_config();

#define PRINT_HEADING(text) fprintf(stderr, "\n  %s:\n", text)
#define PRINT_OPT(opt, ...)                   \
  do {                                        \
    fprintf(stderr, "    %-*s ", OPT_W, opt); \
    fprintf(stderr, __VA_ARGS__);             \
    fprintf(stderr, "\n");                    \
  } while (0)

  /* clang-format off */
  fprintf(stderr, "\nUsage: " CLI_NAME " [OPTIONS...] [PATH]\n");

  PRINT_HEADING("General Options");
  PRINT_OPT("-h, --help", "Show this help message.");
  PRINT_OPT("--version", "Print version and exit.");
  PRINT_OPT("-v, --verbose", "Increase verbosity (can be stacked, e.g., -vvv for maximum verbosity).");
  PRINT_OPT("--dry-run", "Simulate the run without making changes.");

  PRINT_HEADING("Algorithm & Tuning");
  PRINT_OPT("-s, --segments=int",  "Number of segments to hash for each video (default: %zu).", cfg.segments);
  PRINT_OPT("-t, --threshold=int", "Maximum distance threshold (default: %zu).", cfg.threshold);
  PRINT_OPT("",                    "Ranges from 0 to 64 (0 being the most similar).");
  PRINT_OPT("--skip-duration=int", "Skip videos shorter than N seconds (default: %zu).", cfg.skip_duration);

  PRINT_HEADING("Detection");
  PRINT_OPT("--detect-black=bool", "Detect black frames and skip over them (default: %s).",
            ak_flag_has(cfg.detect_flags, DETECT_BLACK_FRAME) ? "true" : "false");
  PRINT_OPT("--detect-bars=bool", "Detect bars around video (e.g. letterboxing) (default: %s).",
            ak_flag_has(cfg.detect_flags, DETECT_BARS) ? "true" : "false");
  PRINT_OPT("--detect-rotation=bool", "Detect rotated videos (default: %s).",
            ak_flag_has(cfg.detect_flags, DETECT_ROTATION) ? "true" : "false");


  PRINT_HEADING("Report");
  PRINT_OPT("--print-hashes", "Print hashes for files in final report.");
  PRINT_OPT("--print-unique", "Include unique files (files without duplicates) in final report.");

  PRINT_HEADING("Execution & Storage");
  PRINT_OPT("--threads=int", "Number of threads to use (uses all available threads by default).");
  PRINT_OPT("--cache=bool", "Database cache should be used (default: %s).",
            ak_flag_has(cfg.runtime_flags, RT_CACHE) ? "true" : "false");
  PRINT_OPT("--progress-bar=bool", "Display a visual progress bar (default: true).");


  fprintf(stderr, "\n  Example:\n    %s\n\n", example);

  /* clang-format on */

#undef PRINT_HEADING
#undef PRINT_OPT
}

/**
 * TODO Make this take a file pointer to print it to the right place
 */
void ak_cli_print_config (ak_config *config) {

  /* This should be larger than the longest configuration option name  */
  const int OPT_W = 28;

#define PRINT_HEADING(text) fprintf(stdout, "\n [%s] \n", text)
#define PRINT_CONFIG_STR(cfg, val) fprintf(stdout, "   %-*s : %s\n", OPT_W, cfg, val)
#define PRINT_CONFIG_ZU(cfg, val) fprintf(stdout, "   %-*s : %zu\n", OPT_W, cfg, val)
#define FLAG_VAL(var, flag) (ak_flag_has((var), (flag)) ? "TRUE" : "FALSE")

  flags32 rtflags = config->runtime_flags;
  flags32 detflags = config->detect_flags;
  flags32 reportflags = config->report_flags;

  /* clang-format off */
  fputc('\n', stdout);
  fputs("+-------- Runtime Configuration --------+", stdout);
  PRINT_HEADING("General");

  /* Verbosity */
  u32 v_level = ak_get_verbosity(rtflags);
  PRINT_CONFIG_ZU("Verbosity", (size_t) v_level);

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
  PRINT_CONFIG_STR("Print Unique Files in Report", FLAG_VAL(reportflags, REPORT_PRINT_UNIQUE_FILES));

  PRINT_HEADING("Detection Flags");
  PRINT_CONFIG_STR("Detect Bars", FLAG_VAL(detflags, DETECT_BARS));
  PRINT_CONFIG_STR("Detect Black Frames", FLAG_VAL(detflags, DETECT_BLACK_FRAME));
  PRINT_CONFIG_STR("Detect Rotation", FLAG_VAL(detflags, DETECT_ROTATION));
  /* clang-format on */
#undef PRINT_HEADING
#undef PRINT_CONFIG_STR
#undef PRINT_CONFIG_ZU
#undef FLAG_VAL

  fputs("+----------------------------------------+\n", stdout);
  fflush(stdout);
}

/* Helper to reverse-lookup long option names */
static AK_PURE const char *get_long_opt_name (int val, const struct option *opts) {
  for (int i = 0; opts[i].name != NULL; i++) {
    if (opts[i].val == val) {
      return opts[i].name;
    }
  }
  return NULL;
}

/* Parses a string to a long, assigns out param (size_t) */
AK_UNUSED static int parse_arg_integer (const char *restrict arg_name,
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
    fprintf(stderr, "[%s] Error: %s requires a valid integer, got '%s'.\n", CLI_NAME, arg_name, arg_str);
    return -1;
  }

  if (errno == ERANGE || val < min || val > max || val > INT_MAX || val < INT_MIN) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME, arg_name, arg_str);

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
static int parse_numeric_arg_sizet (const char *arg_name,
                                    const char *arg_str,
                                    size_t min,
                                    size_t max,
                                    size_t *out) {
  if (!arg_name || !arg_str || !out) {
    return -1;
  }

  const char *p = arg_str;
  /* Skip leading whitespace */
  while (isspace((unsigned char) *p)) {
    ++p;
  }

  /* Prevent negatives from being parsed */
  if (*p == '-') {
    fprintf(stderr, "[%s] Error: %s cannot be negative.\n", CLI_NAME, arg_name);
    return -1;
  }

  char *endptr = NULL;
  errno = 0;

  unsigned long long val = strtoull(arg_str, &endptr, 10);

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "[%s] Error: %s requires a valid positive integer, got '%s'.\n", CLI_NAME, arg_name,
            arg_str);
    return -1;
  }

  if (errno == ERANGE || val > SIZE_MAX || val < min || val > max) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME, arg_name, arg_str);
    if (max == SIZE_MAX) {
      fprintf(stderr, "  Value must be %zu or greater.\n", min);
    } else {
      fprintf(stderr, "  Valid range is %zu to %zu.\n", min, max);
    }
    return -1;
  }

  *out = (size_t) val;
  return 0;
}

/**
 * @brief Parses a string into a boolean (1 or 0).
 * @retval -1 if the string is not a recognized boolean value.
 * @retval 0 if false.
 * @retval 1 if true.
 */
static int parse_bool_arg (const char *arg_name, const char *arg_str) {
  if (!arg_str) {
    return -1;
  }

  assert(arg_name);

  if (strcmp(arg_str, "1") == 0 || strcasecmp(arg_str, "yes") == 0 || strcasecmp(arg_str, "true") == 0) {
    return 1;
  }
  if (strcmp(arg_str, "0") == 0 || strcasecmp(arg_str, "no") == 0 || strcasecmp(arg_str, "false") == 0) {
    return 0;
  }

  fprintf(stderr, "[%s] Error: Invalid argument for boolean option '%s'\n", CLI_NAME, arg_name);
  return -1;
}

static inline int handle_bool_flag (flags32 *flag_var,
                                    flags32 flag_mask,
                                    bool default_value,
                                    const char *restrict arg_name,
                                    const char *restrict arg_val) {
  /* No argument provided: Use default_value */
  if (!arg_val) {
    /* If default value of flag is TRUE */
    if (default_value) {
      *flag_var |= flag_mask;
    } else {
      *flag_var &= ~flag_mask;
    }
    return 0;
  }

  int res = parse_bool_arg(arg_name, arg_val);
  /* Parsing failed */
  if (res == -1) {
    return -1;
  }

  if (res) {
    *flag_var |= flag_mask;
  } else {
    *flag_var &= ~flag_mask;
  }

  return 0;
}

int ak_cli_parse_args (ak_config *config, int argc, char **argv, ak_paths *paths_out) {

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
    FLAG_PROGRESS_BAR,
    FLAG_DETECT_BLACK_FRAME,
    FLAG_DETECT_BARS,
    FLAG_DETECT_ROTATION,
    FLAG_REPORT_PRINT_HASHES,
    FLAG_REPORT_PRINT_UNIQUE,
  };

  /* clang-format off */


  /* name, has_arg, flag, val */
  const struct option anukrta_opts[] = {

/*
 * COMMANDS:
 * These will cause the program to exit early.
 */
    {"help",               no_argument,          NULL,  CMD_HELP},                   // -h | --help
    {"version",            no_argument,          NULL,  CMD_VERSION},                // --version
/*
 * RUNTIME CONFIG:
 * Options to customise how the program does things.
 */
    {"verbose",            optional_argument,    NULL,  FLAG_VERBOSE},               // -v | --verbose
    {"threshold",          required_argument,    NULL,  ARG_THRESHOLD},              // -t | --threshold
    {"segments",           required_argument,    NULL,  ARG_SEGMENTS},               // -s | --segments
    {"threads",            required_argument,    NULL,  ARG_THREADS},                // --threads
    {"skip-duration",      required_argument,    NULL,  ARG_SKIP_DURATION},          // --skip-duration
/*
 * FLAGS
 */
    {"dry-run",            no_argument,          NULL,  FLAG_DRY_RUN},               // --dry-run
    {"print-hashes",       no_argument,          NULL,  FLAG_REPORT_PRINT_HASHES},   // --print-hashes
    {"print-unique",       no_argument,          NULL,  FLAG_REPORT_PRINT_UNIQUE},   // --print-unique
    {"detect-black",       optional_argument,    NULL,  FLAG_DETECT_BLACK_FRAME},    // --detect-black
    {"detect-rotation",    optional_argument,    NULL,  FLAG_DETECT_ROTATION},       // --detect-rotation
    {"detect-bars",        optional_argument,    NULL,  FLAG_DETECT_BARS},           // --detect-bars
    {"cache",              optional_argument,    NULL,  FLAG_CACHE},                 // --cache
    {"progress-bar",       optional_argument,    NULL,  FLAG_PROGRESS_BAR},          // --progress

    {0,                    0,                    0,     0         }};                // END
  /* clang-format on */

  /* Short options */
  /* Start the opt string with ':' to take manual control of errors. */
  char *options_str = ":hvs:t:";

  int option_index = 0;
  int opt;

  int ret = 0;

  size_t available_threads = (size_t) get_available_threads();
  u32 verbosity_level = 0;
  bool explicit_thread_count = false;

  for (;;) {
    option_index = -1;

    // NOLINTBEGIN (concurrency-mt-unsafe)
    opt = getopt_long(argc, argv, options_str, anukrta_opts, &option_index);
    // NOLINTEND

    if (opt == -1) {
      break;
    }

    char arg_invoked[64];
    if (option_index != -1) {
      snprintf(arg_invoked, sizeof(arg_invoked), "--%s", anukrta_opts[option_index].name);
    } else {
      snprintf(arg_invoked, sizeof(arg_invoked), "-%c", opt);
    }

    switch (opt) {
      /* When an option expects an argument but does not receive one */
      case ':':
        {
          const char *long_name = get_long_opt_name(optopt, anukrta_opts);

          fprintf(stderr, "%s: Option '%s' requires an argument.\n", program_name,
                  (long_name ? long_name : arg_invoked));
          fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
          goto exit_error;
        }
      case '?':
        {
          if (optopt != 0) {
            const char *long_name = get_long_opt_name(optopt, anukrta_opts);
            if (long_name) {
              fprintf(stderr, "%s: Unrecognized option '--%s'.\n", program_name, long_name);
            } else {
              fprintf(stderr, "%s: Unrecognized option '-%c'.\n", program_name, optopt);
            }

          } else {
            /* optopt is sometimes 0 for unrecognized long options in certain libc implementations */
            fprintf(stderr, "%s: Unrecognized option.\n", program_name);
          }
          fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
          goto exit_error;
        }

      /* -h | --help */
      case CMD_HELP:
        {
          print_help();
          config->runtime_flags |= RT_EXIT_EARLY;
          return 0;
        }
      /* --version */
      case CMD_VERSION:
        {
          printf("%s - version: " AK_VERSION "\n", program_name);
          config->runtime_flags |= RT_EXIT_EARLY;
          return 0;
        }

      /* -v | --verbose */
      case FLAG_VERBOSE:
        {
          // No argument provided (e.g., -v, -vvv, or --verbose)
          if (optarg == NULL) {
            verbosity_level++;
          } else {
            /* TODO Make this a u32 and create a new u32 parsing function */
            size_t temp_verbosity = 0;

            /*We allow for int_max and then clamp at the end */
            if (parse_numeric_arg_sizet(arg_invoked, optarg, 0, INT_MAX, &temp_verbosity) != 0) {
              goto exit_error;
            }
            verbosity_level = (u32) temp_verbosity;
          }
          break;
        }

      /* --dry-run */
      case FLAG_DRY_RUN:
        {
          config->runtime_flags |= RT_DRY_RUN;
          break;
        }

      /* --cache */
      /* --cache defaults to true if no '=val' is provided */
      case FLAG_CACHE:
        {
          if (handle_bool_flag(&config->runtime_flags, RT_CACHE, true, arg_invoked, optarg) != 0) {
            goto exit_error;
          }
          break;
        }

      /* --progress */
      case FLAG_PROGRESS_BAR:
        {
          if (handle_bool_flag(&config->runtime_flags, RT_PROGRESS_BAR, true, arg_invoked, optarg) != 0) {
            goto exit_error;
          }
          break;
        }
      /* --print-hashes */
      case FLAG_REPORT_PRINT_HASHES:
        {
          config->report_flags |= REPORT_PRINT_HASHES;
          break;
        }

      case FLAG_REPORT_PRINT_UNIQUE:
        {

          /* --print-unique defaults to true if no '=val' is provided */
          if (handle_bool_flag(&config->report_flags, REPORT_PRINT_UNIQUE_FILES, true, arg_invoked,
                               optarg) != 0) {
            goto exit_error;
          }
          break;
        }

        /* -s | --segments */
      case ARG_SEGMENTS:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 1, AK_MAX_VIDEO_SEGMENTS, &config->segments) !=
              0) {
            goto exit_error;
          }
          break;
        }
      /* -t | --threshold */
      case ARG_THRESHOLD:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 0, 64, &config->threshold) != 0) {
            goto exit_error;
          }
          break;
        }

        /* --threads */
      case ARG_THREADS:
        {
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 1, LONG_MAX, &config->thread_count) != 0) {
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
          if (parse_numeric_arg_sizet(arg_invoked, optarg, 0, INT_MAX, &config->skip_duration) != 0) {
            goto exit_error;
          }
          break;
        }
      case FLAG_DETECT_BARS: /* --detect-bars */
        {
          /* --detect-bars defaults to true if no '=val' is provided */
          if (handle_bool_flag(&config->detect_flags, DETECT_BARS, true, arg_invoked, optarg) != 0) {
            goto exit_error;
          }
          break;
        }
      case FLAG_DETECT_BLACK_FRAME: /* --detect-black */
        {

          /* --detect-black defaults to true if no '=val' is provided */
          if (handle_bool_flag(&config->detect_flags, DETECT_BLACK_FRAME, true, arg_invoked, optarg) != 0) {
            goto exit_error;
          }
          break;
        }

      case FLAG_DETECT_ROTATION: /* --detect-rotation */
        {
          /* --detect-rotation defaults to true if no '=val' is provided */
          if (handle_bool_flag(&config->detect_flags, DETECT_ROTATION, true, arg_invoked, optarg) != 0) {
            goto exit_error;
          }
          break;
        }

      default:
        {
          AK_UNREACHABLE(CLI_NAME ": Internal CLI Parsing Error");
        }
    }
  }

  if (verbosity_level > 0) {
    verbosity_level = MINIMUM(verbosity_level, 3);
    ak_set_verbosity(&config->runtime_flags, verbosity_level);
  }

  /* Process remaining positional arguments
   * We assume positional arguments are paths
   */

  int positional_arg_count = argc - optind;
  if (positional_arg_count > 0) {
    printf("\n--- Input Paths (%d) ---\n", positional_arg_count);
    kv_init(*paths_out);

    for (int i = optind; i < argc; i++) {
      kv_push(*paths_out, argv[i]);
      printf("%s\n", argv[i]);
    }

  } else {
    config->runtime_flags |= RT_SCAN_CURR_DIR;
  }

  /* If thread is not explicitly stated, then assign default value (use all available threads) */
  if (!explicit_thread_count) {
    config->thread_count = (size_t) get_available_threads();
  }

  return ret;

exit_error:
  {
    config->runtime_flags |= RT_EXIT_EARLY;
    ret = EINVAL;
    return ret;
  }
}
