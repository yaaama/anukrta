#include "cli.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static char *program_name;

static int extract_program_name (char *arg_zero) {
  char *prog_name = arg_zero;
  char *last_slash = strrchr(prog_name, '/');
  if (!last_slash) {
    return -1;
  }

  program_name = last_slash + 1;
  return 0;
}

static void print_help (void) {

  fprintf(stderr, "Usage: %s [OPTIONS] [DIRECTORY]\n", program_name);
  fprintf(stderr, "\t-h, --help\t\tShow this help message\n");
  fprintf(stderr, "\t-v, --verbose\t\tEnable verbose output\n");
  fprintf(stderr,
          "\t-s, --segments\t\tNumber of segments to hash for each video\n");
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
  printf("Dry Run: %s\n", config->simulate ? "YES" : "NO");
  printf("Detect Black Frames: %s\n",
         config->detect_black_frames ? "YES" : "NO");
  printf("Detect Rotation: %s\n", config->detect_rotation ? "YES" : "NO");
  printf("Segments to hash: %d\n", config->segments);
  printf("Skip videos shorter than: %.1f seconds\n",
         (double) config->skip_duration / (double) ANU_TIME_ONE_SEC_IN_US);
}

int anu_cli_parse_options (anukrta_config *config, int argc, char **argv) {

  extract_program_name(argv[0]);

  int segments = 2;

  /* name, has_arg, flag, val */
  struct option anukrta_opts[] = {
      {"help", no_argument, 0, 'h'},           /* -h | --help */
      {"verbose", no_argument, 0, 'v'},        /* -v | --verbose */
      {"segments", required_argument, 0, 's'}, /* -s | --segments */
      /* Long Options */
      {"version", no_argument, 0, 1001},              /* --version */
      {"dry-run", no_argument, &config->simulate, 1}, /* --dry-run */
      {"detect-black", no_argument, &config->detect_black_frames, 1},
      {"detect-rotation", no_argument, &config->detect_rotation, 1},
      {"skip-duration", required_argument, 0, 1000},
      {0, 0, 0, 0}};

  /* Short options */
  /* Start the optstring with ':' to take manual control of errors. */
  char *options_str = ":hvs:";

  int option_index = 0;

  int opt;
  while ((opt = getopt_long(argc, argv, options_str, anukrta_opts,
                            &option_index)) != -1) {
    switch (opt) {

      /* When an option expects an argument but does not receive one */
      case ':':
        {
          /* optopt holds the character of the flag that failed (e.g., 's') */
          fprintf(stderr, "%s: option '-%c' requires an argument.\n",
                  program_name, optopt);
          fprintf(stderr, "Try '%s --help' for more information.\n",
                  program_name);
          return -1;
        }
      case 0:
        {
          /* This triggers if a flag was set automatically (like --dry-run).
           * We don't necessarily need to do anything here, but we can print info. */
          if (anukrta_opts[option_index].flag != 0) {
            printf("Automatic flag set: --%s\n",
                   anukrta_opts[option_index].name);
          }
          break;
        }

      /* -h | --help */
      case 'h':
        {
          print_help();
          return EXIT_SUCCESS;
        }
      /* -v | --verbose */
      case 'v':
        {
          config->verbose = 1;
          printf("verbose mode...\n");
          break;
        }

      /* -s | --segments */
      case 's':
        {
          char *endptr;
          /* Reset errno to 0 before calling strtol */
          errno = 0;

          /* Convert string (optarg) to a long integer in base 10 */
          long val = strtol(optarg, &endptr, 10);

          /* Check for overflow/underflow */
          if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
            fprintf(stderr, "Error: --segments value '%s' is out of range.\n",
                    optarg);
            return -1;
          }

          /* Check if the user passed non-numeric gibberish.
           * If endptr == optarg, they passed something like "abc"
           * If *endptr != '\0', they passed a mix like "5abc" */
          if (endptr == optarg || *endptr != '\0') {
            fprintf(stderr,
                    "Error: --segments requires a valid integer, got '%s'.\n",
                    optarg);
            return -1;
          }

          /* logic validation (segments shouldn't be negative or 0) */
          if (val <= 0) {
            fprintf(stderr, "Error: --segments must be 1 or greater.\n");
            return -1;
          }

          /* Cast it back to a standard integer */
          segments = (int) val;
          config->segments = segments;
          break;
        }
      /* --version */
      case 1001:
        {
          printf("anukrta version 0.0.0\n");
          return EXIT_SUCCESS;
        }
      case '?':
        {
          printf("Try '%s --help' for more information.\n", program_name);
          return -1;
        }
      default:
        {
          abort(); /* Should never reach here */
        }
    }
  }

  /* Process remaining positional arguments */
  int temp_optind = optind;
  if (optind < argc) {
    printf("\n--- Input Directories (%d) ---\n", argc - optind);
  }
  while (temp_optind < argc) {
    printf("%s\n", argv[temp_optind++]);
  }
  /* Return index of when FILES start */
  return optind;
}
