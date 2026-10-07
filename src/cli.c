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

#define AK_MAX_VERBOSITY 4
#define HELP_OPT_WIDTH 30

/**
 * @brief Identifiers for every CLI option.
 * Values match what getopt_long() returns: short option characters for
 * options that have one, 256+ (outside int value of ascii characters) for long-only options.
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
  AK_OPT_REPORT_FORMAT,

  /** Synthetic id: a positional path argument (never produced by getopt). */
  AK_CLI_OPT_POSITIONAL = 512,
} ak_cli_opt_id;

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
  char *metavar;    /**< Argument placeholder shown in help, or 0. */
  char short_name;  /**< Short option name, or '\0'. */
} ak_cli_opt_def;

/* Section titles appear exactly once each, via CLI_HEADING rows placed
 * directly above their options - an option's section is defined by where
 * it sits in the table, so the two can never drift apart. */
#define CLI_HEADING(text) \
  {.val = 0, .name = NULL, .short_name = 0, .has_arg = no_argument, .metavar = 0, .help = (text)}

static const ak_cli_opt_def cli_opt_defs[] = {
  CLI_HEADING("General Options"),
  /* --help */
  {.val = AK_OPT_HELP,
   .name = "help",
   .short_name = 'h',
   .has_arg = no_argument,
   .metavar = "",
   .help = "Show this help message and exit."},

  /* --version */
  {.val = AK_OPT_VERSION,
   .name = "version",
   .short_name = 0,
   .has_arg = no_argument,
   .metavar = "",
   .help = "Print version and exit."},

  /* --verbose -vvvv*/
  {.val = AK_OPT_VERBOSE,
   .name = "verbose",
   .short_name = 'v',
   .has_arg = optional_argument,
   .metavar = "int",
   .help = "Increase verbosity (repeatable, e.g. -vvvv, or set a level with --verbose=N, where N=[0-4]."},

  /* --dry-run */
  {.val = AK_OPT_DRY_RUN,
   .name = "dry-run",
   .short_name = 0,
   .has_arg = no_argument,
   .metavar = "",
   .help = "Simulate the run without making changes."},

  CLI_HEADING("Algorithm & Tuning"),
  /* --segments -s */
  {.val = AK_OPT_SEGMENTS,
   .name = "segments",
   .short_name = 's',
   .has_arg = required_argument,
   .metavar = "int",
   .help =
       "Number of segments to hash for each video (default: " AK_STRINGIFY(AK_CFG_DEFAULT_SEGMENTS) ")."},

   /* --threshold -t */
   {.val = AK_OPT_THRESHOLD,
    .name = "threshold",
    .short_name = 't',
    .has_arg = required_argument,
    .metavar = "int",
    .help = "Maximum distance threshold, 0 being the most similar (default: " AK_STRINGIFY(AK_CFG_DEFAULT_THRESHOLD) ", range: 0-64)."},

    /* --skip-duration */
    {.val = AK_OPT_SKIP_DURATION,
     .name = "skip-duration",
     .short_name = 0,
     .has_arg = required_argument,
     .metavar = "int",
     .help =
         "Skip videos shorter than N seconds (default: " AK_STRINGIFY(AK_CFG_DEFAULT_SKIP_DURATION) " )."},

     CLI_HEADING("Detection"),

     /* --detect-black */
     {.val = AK_OPT_DETECT_BLACK_FRAME,
      .name = "detect-black",
      .short_name = 0,
      .has_arg = optional_argument,
      .metavar = "bool",
      .help = "Detect black frames and skip over them (default: true)."},

     /* --detect-bars */
     {.val = AK_OPT_DETECT_BARS,
      .name = "detect-bars",
      .short_name = 0,
      .has_arg = optional_argument,
      .metavar = "bool",
      .help = "Detect bars around video, e.g. letterboxing (default: true)."},

     /* --detect-rotation */
     {.val = AK_OPT_DETECT_ROTATION,
      .name = "detect-rotation",
      .short_name = 0,
      .has_arg = optional_argument,
      .metavar = "bool",
      .help = "Detect rotated videos (default: true)."},

     CLI_HEADING("Report"),

     /* --print-hashes */
     {.val = AK_OPT_PRINT_HASHES,
      .name = "print-hashes",
      .short_name = 0,
      .has_arg = optional_argument,
      .metavar = "bool",
      .help = "Print hashes for files in final report (default: false)."},

     /* --print-unique */
     {.val = AK_OPT_PRINT_UNIQUE,
      .name = "print-unique",
      .short_name = 0,
      .has_arg = optional_argument,
      .metavar = "bool",
      .help = "Include unique files in final report (default: true)."},

     /* --format */
     {.val = AK_OPT_REPORT_FORMAT,
      .name = "format",
      .short_name = 0,
      .has_arg = required_argument,
      .metavar = "str",
      .help = "Format of report. Valid outputs: [text, json]. (default: text)"},

     CLI_HEADING("Execution & Storage"),

     /* --threads */
     {.val = AK_OPT_THREADS,
      .name = "threads",
      .short_name = 0,
      .has_arg = required_argument,
      .metavar = "int",
      .help = "Number of threads to use (default: " AK_STRINGIFY(AK_CFG_DEFAULT_THREAD_COUNT) " all available)."},

      /* --cache */
      {.val = AK_OPT_CACHE,
       .name = "cache",
       .short_name = 0,
       .has_arg = optional_argument,
       .metavar = "bool",
       .help = "Use the database cache (default: true)."},

      /* --progress-bar */
      {.val = AK_OPT_PROGRESS_BAR,
       .name = "progress-bar",
       .short_name = 0,
       .has_arg = optional_argument,
       .metavar = "bool",
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

  if (token[0] != '-' || token[1] != '-') {
    if (optopt != 0) {
      fprintf(err, "%s: Unrecognized option '-%c'.\n", program_name, optopt);
    } else {
      fprintf(err, "%s: Unrecognized option '%s'.\n", program_name, token);
    }
    goto hint;
  }

  const char *name = token + 2;
  size_t len = strcspn(name, "=");
  size_t match_count = 0;

  const ak_cli_opt_def *hits[CLI_DEF_COUNT];

  if (len > 0) {
    for (size_t i = 0; i < CLI_DEF_COUNT; i++) {
      const ak_cli_opt_def *def = &cli_opt_defs[i];
      if (def->name && strncmp(def->name, name, len) == 0) {
        hits[match_count] = def;
        match_count++;
      }
    }
  }

  if (match_count == 0) {
    goto unknown;
  }

  if (match_count > 1) {
    fprintf(err, "%s: Option '--%.*s' is ambiguous; it could be '--%s'", program_name, (int) len, name,
            hits[0]->name);
    for (size_t i = 1; i < match_count; i++) {
      fprintf(err, ", '--%s'", hits[i]->name);
    }
    fputs(".\n", err);
    goto hint;
  }

  if (len == strlen(hits[0]->name)) {
    /* An exact spelling only fails this way when given an argument it does not take. */
    fprintf(err, "%s: Option '--%s' doesn't allow an argument.\n", program_name, hits[0]->name);
  } else {
    /* Print that option is unrecognised */
  unknown:
    fprintf(err, "%s: Unrecognized option '%s'.\n", program_name, token);
  }

  /* Print hint to user */
hint:
  fprintf(err, "Try '%s --help' for more information.\n", program_name);
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
    appendf(names, sizeof(names), &n, (def->has_arg == required_argument) ? "=%s" : "[=%s]", def->metavar);
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
#define FLAG_VAL(var, flag) (ak_flag_has((var), (flag)) ? "true" : "false")

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
  PRINT_CONFIG_STR("Report Format", ak_flag_has(reportflags, REPORT_FORMAT_JSON) ? "JSON" : "text");

  PRINT_HEADING("Detection Flags");
  PRINT_CONFIG_STR("Detect Bars", FLAG_VAL(detflags, DETECT_BARS));
  PRINT_CONFIG_STR("Detect Black Frames", FLAG_VAL(detflags, DETECT_BLACK_FRAME));
  PRINT_CONFIG_STR("Detect Rotation", FLAG_VAL(detflags, DETECT_ROTATION));
  /* clang-format on */
#undef PRINT_HEADING
#undef PRINT_CONFIG_STR
#undef PRINT_CONFIG_ZU
#undef PRINT_CONFIG_U32
#undef PRINT_CONFIG_U8
#undef FLAG_VAL

  fputs("+----------------------------------------+\n", out);
  fflush(out);
}

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
      return -1;
    }
    enable = (parsed != 0);
  }

  if (enable) {
    *field |= mask;
  } else {
    *field &= ~mask;
  }
  return 0;
}

/** Special case: -v stacks, --verbose=N sets an explicit level. */
static int verbose_opt (u32 *verbosity, const ak_cli_opt_def *def, const char *arg) {
  if (!arg) {
    (*verbosity)++;
    return 0;
  }

  /* We allow for INT_MAX here and clamp to AK_MAX_VERBOSITY at the end. */
  if (*arg == 'v') {
    while (*arg == 'v') {
      (*verbosity)++;
      arg++;
    }
    return 0;
  }
  /* We allow for INT_MAX here and clamp to AK_MAX_VERBOSITY at the end. */
  size_t level = 0;
  if (parse_size_arg(def, arg, 0, INT_MAX, &level) != 0) {
    return -1;
  }
  *verbosity = (u32) level;
  return 0;
}

static void resolve_threads (ak_config *config, size_t threads, bool threads_explicit) {
  const size_t available = (size_t) get_available_threads();
  size_t count = threads_explicit ? threads : available;

  /* If threads is specified but exceeds the number of available cores */
  if (count > available) {
    fprintf(stderr, "%s: Capping --threads=%zu to the %zu available cores.\n", CLI_NAME, count, available);
    count = available;
  }
  if (count == 0) {
    count = available;
  }
  config->thread_count = count;
}

/**
 * @brief Maps a --format string to the report format flag.
 * @retval 0 on success, -1 on an unrecognised format.
 */
static int report_format_opt (flags32 *report_flags, const ak_cli_opt_def *def, const char *arg) {
  if (strcasecmp(arg, "json") == 0) {
    *report_flags |= REPORT_FORMAT_JSON;
    return 0;
  }
  if (strcasecmp(arg, "text") == 0) {
    *report_flags &= ~REPORT_FORMAT_JSON;
    return 0;
  }

  char invoked[64];
  def_display(invoked, sizeof(invoked), def);
  fprintf(stderr, "[%s] Error: %s expects one of [json, text], got '%s'.\n", CLI_NAME, invoked, arg);
  return -1;
}

ak_cli_action ak_cli_parse (int argc, char **argv, ak_config *config, ak_paths *paths, FILE *err) {
  assert(argv && config && paths && err);
  optind = 0;
  opterr = 0;

  struct option long_opts[CLI_DEF_COUNT + 1];
  /* 'getopt' opt-string, filled by build_getopt_tables().
   * Worst case is ':' + 2 bytes per def (short char + ':' for required_argument) + NUL terminator (1).
   * CLI_DEF_COUNT over-counts - heading rows and long-only
   * options emit nothing - so this is a conservative upper bound that
   * holds no matter how many options are added. */
  char short_opts[(2 * CLI_DEF_COUNT) + 2];
  build_getopt_tables(long_opts, short_opts, AK_ARRAY_SIZE(short_opts));

  kv_init(*paths);

  /* Deferred state, we finalise their values at the end. */
  u32 verbosity = 0;
  size_t threads = 0;
  bool threads_explicit = false;

  for (;;) {
    int long_index = -1;
    // NOLINTNEXTLINE (concurrency-mt-unsafe)
    int opt = getopt_long(argc, argv, short_opts, long_opts, &long_index);

    /* getopt_long fails just break */
    if (opt == -1) {
      break;
    }
    /* If argument is not specified with option, then fail parsing. */
    if (opt == ':') {
      report_missing_arg(err, argv[0], argv);
      return AK_CLI_EXIT_FAIL;
    }
    /* If we receive an unknown option, then fail. */
    if (opt == '?') {
      report_unknown_opt(err, argv[0], argv);
      return AK_CLI_EXIT_FAIL;
    }

    /* Find definition for the option that the user specified */
    const ak_cli_opt_def *def = find_def(opt);
    if (!def) {
      AK_UNREACHABLE(CLI_NAME ": getopt returned an unknown option id");
    }

    AK_CHECK(def->name != NULL);

    /* Reject abbreviations */
    if (long_index >= 0) {
      const char *token = (optarg == argv[optind - 1]) ? argv[optind - 2] : argv[optind - 1];
      const char *typed = token + 2;
      size_t typed_len = strcspn(typed, "=");
      if (typed_len != strlen(def->name) || strncmp(typed, def->name, typed_len) != 0) {
        report_abbreviation(err, argv[0], typed, typed_len, def);
        return AK_CLI_EXIT_FAIL;
      }
    }

    int rc = 0;
    switch (opt) {
      case AK_OPT_HELP:
        print_help(stdout);
        return AK_CLI_EXIT_OK;
      case AK_OPT_VERSION:
        printf("%s - version: " AK_VERSION_STR "\n", argv[0]);
        return AK_CLI_EXIT_OK;
      case AK_OPT_VERBOSE:
        rc = verbose_opt(&verbosity, def, optarg); /* now takes u32*, returns 0/-1 */
        break;
      case AK_OPT_THREADS:
        rc = parse_size_arg(def, optarg, 0, UINT8_MAX, &threads);
        threads_explicit = (rc == 0);
        break;
      case AK_OPT_SEGMENTS:
        rc = parse_size_arg(def, optarg, 1, AK_MAX_VIDEO_SEGMENTS, &config->segments);
        break;
      case AK_OPT_CACHE:
        rc = bool_flag(&config->runtime_flags, RT_CACHE, true, def, optarg);
        break;

      case AK_OPT_PROGRESS_BAR:
        rc = bool_flag(&config->runtime_flags, RT_PROGRESS_BAR, true, def, optarg);
        break;

      case AK_OPT_PRINT_HASHES:
        rc = bool_flag(&config->report_flags, REPORT_PRINT_HASHES, true, def, optarg);
        break;

      case AK_OPT_PRINT_UNIQUE:
        rc = bool_flag(&config->report_flags, REPORT_PRINT_UNIQUE_FILES, true, def, optarg);
        break;
      case AK_OPT_REPORT_FORMAT:
        rc = report_format_opt(&config->report_flags, def, optarg);
        break;

      case AK_OPT_DETECT_BLACK_FRAME:
        rc = bool_flag(&config->detect_flags, DETECT_BLACK_FRAME, true, def, optarg);
        break;

      case AK_OPT_DETECT_BARS:
        rc = bool_flag(&config->detect_flags, DETECT_BARS, true, def, optarg);
        break;

      case AK_OPT_DETECT_ROTATION:
        rc = bool_flag(&config->detect_flags, DETECT_ROTATION, true, def, optarg);
        break;

      case AK_OPT_THRESHOLD:
        rc = parse_size_arg(def, optarg, AK_HAMMING_MIN, AK_HAMMING_MAX, &config->threshold);
        break;

      case AK_OPT_SKIP_DURATION:
        rc = parse_size_arg(def, optarg, 0, INT_MAX, &config->skip_duration);
        break;

      default:
        AK_UNREACHABLE(CLI_NAME ": unknown option id");
        break;
    }
    if (rc != 0) {
      return AK_CLI_EXIT_FAIL;
    }
  }

  /* Everything after options is considered a path (our positional arguments). */
  for (int i = optind; i < argc; i++) {
    kv_push(*paths, argv[i]);
  }

  /* If we do not have any paths, then fall back to scanning current directory. */
  if (kv_size(*paths) == 0) {
    config->runtime_flags |= RT_SCAN_CURR_DIR;
  }

  /* Clamp verbosity to a valid value */
  if (verbosity > 0) {
    config->verbosity = (u8) MINIMUM(verbosity, AK_MAX_VERBOSITY);
  }

  /* Resolve our threads value to a valid value. */
  resolve_threads(config, threads, threads_explicit);
  return AK_CLI_RUN;
}
