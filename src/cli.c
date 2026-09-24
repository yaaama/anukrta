#include "cli.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>  // IWYU pragma: keep
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
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

#define AK_MAX_VERBOSITY 4
#define HELP_OPT_WIDTH 30

/**
 * @brief Identifiers for every CLI option.
 * Values match what getopt_long() returns: short option characters for
 * options that have one, 256+ for long-only options.
 */
typedef enum ak_cli_opt_id {  // NOLINT (*enum-initial-value)
  AK_OPT_HELP = 'h',
  AK_OPT_SEGMENTS = 's',
  AK_OPT_THRESHOLD = 't',
  AK_OPT_VERBOSE = 'v',

  /* Auto-incrementing long-only options */
  AK_OPT_VERSION = 256,
  AK_OPT_THREADS,
  AK_OPT_SKIP_DURATION,
  AK_OPT_DRY_RUN,
  AK_OPT_CACHE,
  AK_OPT_PROGRESS_BAR,
  AK_OPT_DETECT_BLACK_FRAME,
  AK_OPT_DETECT_BARS,
  AK_OPT_DETECT_ROTATION,
  AK_OPT_PRINT_HASHES,
  AK_OPT_PRINT_UNIQUE,

  /** Synthetic id: a positional path argument (never produced by getopt). */
  AK_CLI_OPT_POSITIONAL = 512,
} ak_cli_opt_id;

enum {
  METAVAR_NONE = 0,
  METAVAR_USIZE,
  METAVAR_BOOL,
  __METAVAR_END
};

static const char *metavars[__METAVAR_END] = {[METAVAR_NONE] = "",
                                              [METAVAR_USIZE] = "int",
                                              [METAVAR_BOOL] = "bool"};

/*
 * A row with `name == NULL` is a section-heading row: it holds a section
 * title in `help`, groups the options that follow it, and is skipped by
 * getopt_long().
 */
typedef struct ak_cli_opt_def {
  const char *name; /**< Long option name, or NULL for a heading row. */
  const char *help; /**< One-line description, or the section title on heading rows. */
  int val;          /**< Unique id: the short char, or 256+ for long-only. */
  int has_arg;      /**< no_argument / required_argument / optional_argument. */
  int metavar;      /**< Argument placeholder shown in help, or 0. */
  char short_name;  /**< Short option name, or '\0'. */
} ak_cli_opt_def;

/* Section titles appear exactly once each, via CLI_HEADING rows placed
 * directly above their options - an option's section is defined by where
 * it sits in the table, so the two can never drift apart. */
#define CLI_HEADING(text) \
  {.val = 0, .name = NULL, .short_name = '\0', .has_arg = no_argument, .metavar = 0, .help = (text)}

static const ak_cli_opt_def cli_opt_defs[] = {
  CLI_HEADING("General Options"),
  /* --help */
  {.val = AK_OPT_HELP,
   .name = "help",
   .short_name = 'h',
   .has_arg = no_argument,
   .metavar = METAVAR_NONE,
   .help = "Show this help message and exit."},

  /* --version */
  {.val = AK_OPT_VERSION,
   .name = "version",
   .short_name = '\0',
   .has_arg = no_argument,
   .metavar = METAVAR_NONE,
   .help = "Print version and exit."},

  /* --verbose -vvvv*/
  {.val = AK_OPT_VERBOSE,
   .name = "verbose",
   .short_name = 'v',
   .has_arg = optional_argument,
   .metavar = METAVAR_USIZE,
   .help = "Increase verbosity (repeatable, e.g. -vvvv, or set a level with --verbose=N, where N=[0-4]."},

  /* --dry-run */
  {.val = AK_OPT_DRY_RUN,
   .name = "dry-run",
   .short_name = '\0',
   .has_arg = no_argument,
   .metavar = METAVAR_NONE,
   .help = "Simulate the run without making changes."},

  CLI_HEADING("Algorithm & Tuning"),
  /* --segments -s */
  {.val = AK_OPT_SEGMENTS,
   .name = "segments",
   .short_name = 's',
   .has_arg = required_argument,
   .metavar = METAVAR_USIZE,
   .help =
       "Number of segments to hash for each video (default: " AK_STRINGIFY(AK_CFG_DEFAULT_SEGMENTS) ")."},

   /* --threshold -t */
   {.val = AK_OPT_THRESHOLD,
    .name = "threshold",
    .short_name = 't',
    .has_arg = required_argument,
    .metavar = METAVAR_USIZE,
    .help = "Maximum distance threshold, 0 being the most similar (default: " AK_STRINGIFY(AK_CFG_DEFAULT_THRESHOLD) ", range: 0-64)."},

    /* --skip-duration */
    {.val = AK_OPT_SKIP_DURATION,
     .name = "skip-duration",
     .short_name = '\0',
     .has_arg = required_argument,
     .metavar = METAVAR_USIZE,
     .help =
         "Skip videos shorter than N seconds (default: " AK_STRINGIFY(AK_CFG_DEFAULT_SKIP_DURATION) " )."},

     CLI_HEADING("Detection"),

     /* --detect-black */
     {.val = AK_OPT_DETECT_BLACK_FRAME,
      .name = "detect-black",
      .short_name = '\0',
      .has_arg = optional_argument,
      .metavar = METAVAR_BOOL,
      .help = "Detect black frames and skip over them (default: true)."},

     /* --detect-bars */
     {.val = AK_OPT_DETECT_BARS,
      .name = "detect-bars",
      .short_name = '\0',
      .has_arg = optional_argument,
      .metavar = METAVAR_BOOL,
      .help = "Detect bars around video, e.g. letterboxing (default: true)."},

     /* --detect-rotation */
     {.val = AK_OPT_DETECT_ROTATION,
      .name = "detect-rotation",
      .short_name = '\0',
      .has_arg = optional_argument,
      .metavar = METAVAR_BOOL,
      .help = "Detect rotated videos (default: true)."},

     CLI_HEADING("Report"),

     /* --print-hashes */
     {.val = AK_OPT_PRINT_HASHES,
      .name = "print-hashes",
      .short_name = '\0',
      .has_arg = optional_argument,
      .metavar = METAVAR_BOOL,
      .help = "Print hashes for files in final report (default: false)."},

     /* --print-unique */
     {.val = AK_OPT_PRINT_UNIQUE,
      .name = "print-unique",
      .short_name = '\0',
      .has_arg = optional_argument,
      .metavar = METAVAR_BOOL,
      .help = "Include unique files in final report (default: true)."},

     CLI_HEADING("Execution & Storage"),

     /* --threads */
     {.val = AK_OPT_THREADS,
      .name = "threads",
      .short_name = '\0',
      .has_arg = required_argument,
      .metavar = METAVAR_USIZE,
      .help = "Number of threads to use (default: " AK_STRINGIFY(AK_CFG_DEFAULT_THREAD_COUNT) " all available)."},

      /* --cache */
      {.val = AK_OPT_CACHE,
       .name = "cache",
       .short_name = '\0',
       .has_arg = optional_argument,
       .metavar = METAVAR_BOOL,
       .help = "Use the database cache (default: true)."},

      /* --progress-bar */
      {.val = AK_OPT_PROGRESS_BAR,
       .name = "progress-bar",
       .short_name = '\0',
       .has_arg = optional_argument,
       .metavar = METAVAR_BOOL,
       .help = "Display a visual progress bar (default: true)."},
};
#undef CLI_HEADING

#define CLI_DEF_COUNT AK_ARRAY_SIZE(cli_opt_defs)

/** Reverse-lookup: finds the definition for an option id. */
static const ak_cli_opt_def *find_def (int opt_id) {
  for (size_t i = 0; i < CLI_DEF_COUNT; i++) {
    if (cli_opt_defs[i].val == opt_id) {
      return &cli_opt_defs[i];
    }
  }
  return NULL;
}

/** Formats how an option was invoked ("-s" / "--segments") for diagnostics. */
static void def_display (char *buf, size_t len, const ak_cli_opt_def *def) {
  if (def->short_name) {
    snprintf(buf, len, "-%c", def->short_name);
  } else {
    snprintf(buf, len, "--%s", def->name);
  }
}

static long get_available_threads (void) {
  errno = 0;
  long cores = sysconf(_SC_NPROCESSORS_ONLN);

  if (cores < 1) {
    if (errno != 0) {
      perror("Failed to retrieve core-count");
    } else {
      fprintf(stderr, "Could not determine core-count.\n");
    }
  }
  return MAXIMUM(cores, 1);
}

/* Helper to reverse-lookup metavar strings */
static AK_PURE const char *get_metavar_str (int val) {
  assert(val >= 0 && val < __METAVAR_END);
  return metavars[val];
}

/** Parses a string to a size_t within [min, max], storing it in @p *out. */
static int parse_size_arg (const ak_cli_opt_def *def,
                           const char *arg_str,
                           size_t min,
                           size_t max,
                           size_t *out) {
  assert(def && out);

  char invoked[64];
  def_display(invoked, sizeof(invoked), def);

  if (!arg_str) {
    fprintf(stderr, "[%s] Error: %s requires an argument.\n", CLI_NAME, invoked);
    return -1;
  }

  const char *p = arg_str;
  /* Skip leading whitespace */
  while (isspace((unsigned char) *p)) {
    ++p;
  }

  /* Prevent negatives from being parsed */
  if (*p == '-') {
    fprintf(stderr, "[%s] Error: %s cannot be negative.\n", CLI_NAME, invoked);
    return -1;
  }

  char *endptr = NULL;
  errno = 0;

  unsigned long long val = strtoull(arg_str, &endptr, 10);

  if (endptr == arg_str || *endptr != '\0') {
    fprintf(stderr, "[%s] Error: %s requires a valid positive integer, got '%s'.\n", CLI_NAME, invoked,
            arg_str);
    return -1;
  }

  if (errno == ERANGE || val < min || val > max) {
    fprintf(stderr, "[%s] Error: %s value '%s' is out of range.\n", CLI_NAME, invoked, arg_str);
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
static int parse_bool_arg (const ak_cli_opt_def *def, const char *arg_str) {
  assert(def);

  if (!arg_str) {
    return -1;
  }

  if (strcmp(arg_str, "1") == 0 || strcasecmp(arg_str, "yes") == 0 || strcasecmp(arg_str, "true") == 0 ||
      strcasecmp(arg_str, "on") == 0) {
    return 1;
  }
  if (strcmp(arg_str, "0") == 0 || strcasecmp(arg_str, "no") == 0 || strcasecmp(arg_str, "false") == 0 ||
      strcasecmp(arg_str, "off") == 0) {
    return 0;
  }

  char invoked[64];
  def_display(invoked, sizeof invoked, def);
  fprintf(stderr, "[%s] Error: %s expects a boolean (yes/no, true/false, on/off, 1/0), got '%s'.\n",
          CLI_NAME, invoked, arg_str);
  return -1;
}

/** Builds the getopt_long() argument tables from `cli_opt_defs[]`. */
static void build_getopt_tables (struct option *long_opts, char *short_opts, size_t short_opts_capacity) {
  char *s = short_opts;
  /* Start the optstring with ':' to take manual control of errors. */
  *s++ = ':';

  size_t n = 0; /* number of real (non-heading) options written */
  for (size_t i = 0; i < CLI_DEF_COUNT; i++) {
    const ak_cli_opt_def *def = &cli_opt_defs[i];

    if (!def->name) {
      continue; /* section heading row */
    }

    long_opts[n].name = def->name;
    long_opts[n].has_arg = def->has_arg;
    long_opts[n].flag = NULL;
    long_opts[n].val = def->val;

    if (def->short_name) {
      *s++ = def->short_name;
      /* Only required_argument short options get ':' in the optstring.
       * An optional-argument short option would swallow attached characters
       * (e.g. "-vt" would be read as "-v t"), so we deliberately do not
       * enable them - "--verbose=N" still works via the long form. */
      if (def->has_arg == required_argument) {
        *s++ = ':';
      }
    }
    n++;
  }
  *s = '\0';
  assert((size_t) (s - short_opts) < short_opts_capacity);
  long_opts[n] = (struct option){0};
}

static void report_missing_arg (FILE *err, const char *program_name, char **argv) {
  /* getopt cannot tell us whether a long or short spelling was used
   * (optopt holds the shared val), but argv[optind - 1] holds the token
   * exactly as the user typed it. */

  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  const char *token = (optind > 0) ? argv[optind - 1] : "?";

  fprintf(err, "%s: Option '%s' requires an argument.\n", program_name, token);
  fprintf(err, "Try '%s --help' for more information.\n", program_name);
}

static void report_abbreviation (FILE *err,
                                 const char *program_name,
                                 const char *typed,
                                 size_t typed_len,
                                 const ak_cli_opt_def *def) {
  fprintf(err, "%s: Option '--%.*s' is unknown. Did you mean '--%s'?\n", program_name, (int) typed_len,
          typed, def->name);
}

static void report_unknown_opt (FILE *err, const char *program_name, char **argv) {

  const char *token = (optind > 0) ? argv[optind - 1] : "?";

  if (token[0] == '-' && token[1] == '-') {
    const char *name = token + 2;
    size_t len = strcspn(name, "=");

    const ak_cli_opt_def *first = NULL;
    const ak_cli_opt_def *hits[CLI_DEF_COUNT];

    size_t match_count = 0;

    if (len > 0) {
      for (size_t i = 0; i < CLI_DEF_COUNT; i++) {
        const ak_cli_opt_def *def = &cli_opt_defs[i];
        if (def->name && strncmp(def->name, name, len) == 0) {
          hits[match_count] = def;
          match_count++;
        }
      }
    }

    if (match_count > 0) {
      first = hits[0];
    }

    if (match_count > 1) {
      fprintf(err, "%s: Option '--%.*s' is ambiguous; it could be '--%s'", program_name, (int) len, name,
              first->name);
      for (size_t i = 1; i < match_count; i++) {
        fprintf(err, ", '--%s'", hits[i]->name);
      }

      fputs(".\n", err);

    } else if (match_count == 1 && len == strlen(first->name)) {
      /* An exact spelling only fails this way when given an argument it does not take. */
      fprintf(err, "%s: Option '--%s' doesn't allow an argument.\n", program_name, first->name);
    } else {
      fprintf(err, "%s: Unrecognized option '%s'.\n", program_name, token);
    }
  } else if (optopt != 0) {
    fprintf(err, "%s: Unrecognized option '-%c'.\n", program_name, optopt);
  } else {
    fprintf(err, "%s: Unrecognized option '%s'.\n", program_name, token);
  }
  fprintf(err, "Try '%s --help' for more information.\n", program_name);
}

/**
 * Tokenise CLI arguments passed in.
 * @warn Caller must destroy @p events_out regardless of return value.
 */
int ak_cli_tokenize (int argc, char **argv, ak_cli_events *events_out, FILE *err) {
  assert(argv && events_out && err);

  optind = 0; /* reset global state this makes repeated calls safe */
  opterr = 0; /* all error reporting is ours */

  struct option long_opts[CLI_DEF_COUNT + 1];
  /* getopt optstring, filled by build_getopt_tables().
   * Worst case is ':' (1) + 2 bytes per def (short char + ':' for required_argument)
   * + NUL terminator (1).
   * CLI_DEF_COUNT over-counts - heading rows and long-only
   * options emit nothing - so this is a conservative upper bound that
   * holds no matter how many options are added. */
  char short_opts[(2 * CLI_DEF_COUNT) + 2];
  build_getopt_tables(long_opts, short_opts, AK_ARRAY_SIZE(short_opts));

  kv_init(*events_out);

  for (;;) {
    // NOLINTNEXTLINE (concurrency-mt-unsafe)
    int opt = getopt_long(argc, argv, short_opts, long_opts, NULL);

    if (opt == -1) {
      break;
    }
    if (opt == ':') {
      report_missing_arg(err, argv[0], argv);
      return -1;
    }
    if (opt == '?') {
      report_unknown_opt(err, argv[0], argv);
      return -1;
    }

    const ak_cli_opt_def *def = find_def(opt);
    if (!def) {
      AK_UNREACHABLE(CLI_NAME ": getopt returned an unknown option id");
    }
    /* getopt_long() accepts unambiguous prefixes (e.g. "--he" for "--help");
     * we require long options to be spelled out in full. The token getopt
     * just processed sits at argv[optind - 1]. */
    // NOLINTNEXTLINE (concurrency-mt-unsafe)
    const char *token = (optind > 0) ? argv[optind - 1] : "?";
    if (token[0] == '-' && token[1] == '-') {
      const char *typed = token + 2;
      size_t typed_len = strcspn(typed, "=");
      if (typed_len != strlen(def->name) || strncmp(typed, def->name, typed_len) != 0) {
        report_abbreviation(err, argv[0], typed, typed_len, def);
        return -1;
      }
    }

    ak_cli_event event = {.opt_id = opt, .arg = optarg, .def_index = opt};
    kv_push(*events_out, event);
  }

  /* Everything after the options is treated as a path. */
  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  for (int i = optind; i < argc; i++) {
    ak_cli_event event = {.opt_id = AK_CLI_OPT_POSITIONAL, .arg = argv[i]};
    kv_push(*events_out, event);
  }

  return 0;
}

/* -------------------------------------------------------------------------- */
/* 5. Help & config output                                                    */
/* -------------------------------------------------------------------------- */

AK_PRINTF(4, 5) static void appendf (char *buf, size_t buf_sz, int *off, const char *fmt, ...) {
  if (*off < 0 || (size_t) *off >= buf_sz) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  // NOLINTNEXTLINE(clang-analyzer-security.VAList): false positive, va_start is on the line above
  int r = vsnprintf(buf + *off, buf_sz - (size_t) *off, fmt, ap);
  va_end(ap);
  if (r >= 0) {
    int x = *off + r;
    int y = (int) buf_sz - 1;
    *off = MINIMUM(x, y);
  }
}

/** Prints one option's help line. */
static void print_option_help (FILE *out, const ak_cli_opt_def *def) {
  char names[64];
  int n = 0;

  if (def->short_name) {
    appendf(names, sizeof(names), &n, "-%c, ", def->short_name);
  }

  appendf(names, sizeof(names), &n, "--%s", def->name);

  if (def->metavar) {
    appendf(names, sizeof(names), &n, (def->has_arg == required_argument) ? "=%s" : "[=%s]",
            get_metavar_str(def->metavar));
  }
  fprintf(out, "    %-*s %s\n", HELP_OPT_WIDTH, names, def->help);
}

static void print_help (FILE *out) {
  static const char *example =
      CLI_NAME " --cache=no --verbose --segments=5 -- /dir/one/ /dir/two/ videoFile.mp4";

  fprintf(out, "\nUsage: " CLI_NAME " [OPTIONS...] -- [PATH]...\n");

  for (size_t i = 0; i < CLI_DEF_COUNT; i++) {
    const ak_cli_opt_def *def = &cli_opt_defs[i];
    if (!def->name) {
      fprintf(out, "\n  %s:\n", def->help); /* section heading row */
      continue;
    }
    print_option_help(out, def);
  }

  fprintf(out, "\n  Example:\n    %s\n\n", example);
  fprintf(out, "\n  Note: It's recommended to precede positional arguments (paths) with '--'.\n");
}

void ak_cli_print_config (FILE *out, const ak_config *config) {

  /* This should be larger than the longest configuration option name  */
  const int OPT_W = 28;

#define PRINT_HEADING(text) fprintf(out, "\n [%s] \n", text)
#define PRINT_CONFIG_STR(cfg, val) fprintf(out, "   %-*s : %s\n", OPT_W, cfg, val)
#define PRINT_CONFIG_ZU(cfg, val) fprintf(out, "   %-*s : %zu\n", OPT_W, cfg, val)
#define PRINT_CONFIG_U32(cfg, val) fprintf(out, "   %-*s : %" PRIu32 "\n", OPT_W, cfg, val)
#define PRINT_CONFIG_U8(cfg, val) fprintf(out, "   %-*s : %" PRIu8 "\n", OPT_W, cfg, val)
#define FLAG_VAL(var, flag) (ak_flag_has((var), (flag)) ? "TRUE" : "FALSE")

  flags32 rtflags = config->runtime_flags;
  flags32 detflags = config->detect_flags;
  flags32 reportflags = config->report_flags;

  /* clang-format off */
  fputc('\n', out);
  fputs("+-------- Runtime Configuration --------+", out);
  PRINT_HEADING("General");

  /* Verbosity */
  PRINT_CONFIG_U8("Verbosity", config->verbosity);

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

  fputs("+----------------------------------------+\n", out);
  fflush(out);
}

/* -------------------------------------------------------------------------- */
/* 6. Phase 2: applier                                                        */
/* -------------------------------------------------------------------------- */

/** Working state accumulated while applying events. */
typedef struct cli_ctx {
  ak_config *config;
  u32 verbosity;         /* accumulated -v / --verbose level, clamped later */
  size_t threads;        /* value given to --threads */
  bool threads_explicit; /* whether --threads was given at all */
} cli_ctx;

/** Return codes for individual event application. */
typedef enum apply_result {
  APPLY_ERROR = -1, /**< Invalid value; abort with AK_CLI_EXIT_FAIL. */
  APPLY_OK = 0,     /**< Event applied; continue. */
  APPLY_STOP = 1,   /**< Early-exit command (help/version); stop, exit 0. */
} apply_result;

/** Sets or clears a flag bit; bare use of the option applies `assume`. */
static int bool_flag (flags32 *field,
                      flags32 mask,
                      bool assume,
                      const ak_cli_opt_def *def,
                      const char *arg) {
  bool enable = assume;
  if (arg) {
    int parsed = parse_bool_arg(def, arg);
    if (parsed < 0) {
      return APPLY_ERROR;
    }
    enable = (parsed != 0);
  }

  if (enable) {
    *field |= mask;
  } else {
    *field &= ~mask;
  }
  return APPLY_OK;
}

/** Special case: -v stacks, --verbose=N sets an explicit level. */
static int verbose_opt (cli_ctx *ctx, const ak_cli_opt_def *def, const char *arg) {
  if (!arg) {
    ctx->verbosity++;
    return APPLY_OK;
  }

  /* We allow for INT_MAX here and clamp to AK_MAX_VERBOSITY at the end. */
  size_t level = 0;
  if (parse_size_arg(def, arg, 0, INT_MAX, &level) != 0) {
    return APPLY_ERROR;
  }
  ctx->verbosity = (u32) level;
  return APPLY_OK;
}

/** Special case: --threads needs post-processing once parsing finishes. */
static int threads_opt (cli_ctx *ctx, const ak_cli_opt_def *def, const char *arg) {
  if (parse_size_arg(def, arg, 0, SIZE_MAX, &ctx->threads) != 0) {
    return APPLY_ERROR;
  }
  ctx->threads_explicit = true;
  return APPLY_OK;
}

static int apply_event (cli_ctx *ctx,
                        const ak_cli_event *event,
                        const char *program_name,
                        ak_paths *paths) {

  ak_config *config = ctx->config;
  const ak_cli_opt_def *def = &cli_opt_defs[event->def_index];

  switch (event->opt_id) {
    case AK_OPT_HELP:
      print_help(stdout);
      return APPLY_STOP;

    case AK_OPT_VERSION:
      printf("%s - version: " AK_VERSION "\n", program_name);
      return APPLY_STOP;

    case AK_OPT_VERBOSE:
      return verbose_opt(ctx, def, event->arg);

    case AK_OPT_THREADS:
      return threads_opt(ctx, def, event->arg);

    case AK_OPT_DRY_RUN:
      config->runtime_flags |= RT_DRY_RUN;
      return APPLY_OK;

    case AK_OPT_CACHE:
      return bool_flag(&config->runtime_flags, RT_CACHE, true, def, event->arg);

    case AK_OPT_PROGRESS_BAR:
      return bool_flag(&config->runtime_flags, RT_PROGRESS_BAR, true, def, event->arg);

    case AK_OPT_PRINT_HASHES:
      return bool_flag(&config->report_flags, REPORT_PRINT_HASHES, true, def, event->arg);

    case AK_OPT_PRINT_UNIQUE:
      return bool_flag(&config->report_flags, REPORT_PRINT_UNIQUE_FILES, true, def, event->arg);

    case AK_OPT_DETECT_BLACK_FRAME:
      return bool_flag(&config->detect_flags, DETECT_BLACK_FRAME, true, def, event->arg);

    case AK_OPT_DETECT_BARS:
      return bool_flag(&config->detect_flags, DETECT_BARS, true, def, event->arg);

    case AK_OPT_DETECT_ROTATION:
      return bool_flag(&config->detect_flags, DETECT_ROTATION, true, def, event->arg);

    case AK_OPT_SEGMENTS:
      return parse_size_arg(def, event->arg, 1, AK_MAX_VIDEO_SEGMENTS, &config->segments);

    case AK_OPT_THRESHOLD:
      return parse_size_arg(def, event->arg, AK_HAMMING_MIN, AK_HAMMING_MAX, &config->threshold);

    case AK_OPT_SKIP_DURATION:
      return parse_size_arg(def, event->arg, 0, INT_MAX, &config->skip_duration);

    case AK_CLI_OPT_POSITIONAL:
      kv_push(*paths, event->arg);
      return APPLY_OK;

    default:
      AK_UNREACHABLE(CLI_NAME ": unknown option id in event stream");
  }
}

static void finalise_verbosity (cli_ctx *ctx) {
  if (ctx->verbosity == 0) {
    return;
  }
  /* Clamp verbosity to being 4 or less. */
  u32 clamped = MINIMUM(ctx->verbosity, AK_MAX_VERBOSITY);
  ctx->config->verbosity = (u8) clamped;
}

/** If no paths were supplied, fall back to scanning the current directory. */
static void finalise_paths (ak_config *config, const ak_paths *paths) {
  if (kv_size(*paths) == 0) {
    config->runtime_flags |= RT_SCAN_CURR_DIR;
  }
}

static void finalise_threads (cli_ctx *ctx) {
  const size_t available = (size_t) get_available_threads();
  size_t threads = ctx->threads_explicit ? ctx->threads : available;

  /* If threads is specified but exceeds the number of available cores */
  if (threads > available) {
    fprintf(stderr, "%s: Capping --threads=%zu to the %zu available cores.\n", CLI_NAME, threads,
            available);
    threads = available;
  }
  if (threads == 0) {
    threads = available;
  }
  ctx->config->thread_count = threads;
}

ak_cli_action ak_cli_apply (ak_config *config,
                            const ak_cli_events *events,
                            const char *program_name,
                            ak_paths *paths_out) {
  assert(config && events && paths_out);

  cli_ctx ctx = {.config = config};
  kv_init(*paths_out);

  size_t events_len = kv_size(*events);
  for (size_t i = 0; i < events_len; i++) {
    const ak_cli_event *event = &kv_A(*events, i);
    apply_result rc = apply_event(&ctx, event, program_name, paths_out);
    if (rc == APPLY_STOP) {
      return AK_CLI_EXIT_OK;
    }
    if (rc == APPLY_ERROR) {
      return AK_CLI_EXIT_FAIL;
    }
  }

  finalise_verbosity(&ctx);
  finalise_paths(config, paths_out);
  finalise_threads(&ctx);
  return AK_CLI_RUN;
}
