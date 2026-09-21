#include "term.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"

/**
 * Get environment variable and parse it as an integer.
 * @note Taken from: https://github.com/util-linux/util-linux/blob/master/lib/ttyutils.c
 */
static int get_env_int (const char *name) {
  const char *cp = getenv(name);

  if (cp) {
    char *end = NULL;
    long x;

    errno = 0;
    x = strtol(cp, &end, 10);

    if (errno == 0 && end && *end == '\0' && end > cp && x > 0 && x <= INT_MAX) {
      return (int) x;
    }
  }
  return -1;
}

static bool env_term_is_dumb (void) {

  const char *term_env = getenv("TERM");

  /* If we don't find a term environment variable */
  if (!term_env || term_env[0] == '\0') {
    return true;
  }
  /* If term is actually dumb */
  if (strcmp(term_env, "dumb") == 0) {
    return true;
  }

  return false;
}

static bool env_nocolor_defined (void) {
  return (getenv("NO_COLOR") != NULL);
}

static int try_get_terminal_dimensions (int termfd, int *restrict columns, int *restrict lines) {

  const int default_cols = 40;
  const int default_lines = 40;

  int c = 0;
  int l = 0;

  struct winsize w_win;

  if (ioctl(termfd, TIOCGWINSZ, &w_win) == 0) {
    c = w_win.ws_col;
    l = w_win.ws_row;
  }

  if (c <= 0) {
    c = get_env_int("COLUMNS");
  }
  if (l <= 0) {
    l = get_env_int("LINES");
  }

  *columns = c > 0 ? c : default_cols;
  *lines = l > 0 ? l : default_lines;

  /* Return whether terminal dimensions were actually found */
  return c > 0 && l > 0;
}

static bool query_winsize (int fd, int *cols, int *rows) {
  struct winsize ws;
  if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
    return false;
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
  return true;
}

/* Call this at the top of every render tick. */
bool ak_term_ctx_update (ak_term_ctx *ctx) {
  int cols;
  int rows;
  if (!query_winsize(STDOUT_FILENO, &cols, &rows)) {
    return false; /* ioctl failed: keep last known size */
  }
  bool resized = (cols != ctx->term_width) || (rows != ctx->term_height);
  ctx->term_width = cols;
  ctx->term_height = rows;
  return resized;
}

void ak_term_ctx_destroy (ak_term_ctx *ctx) {
  if (!ctx) {
    return;
  }

  free(ctx);
}

int ak_term_ctx_init (ak_term_ctx *ctx) {

  /* Set defaults */
  *ctx = (ak_term_ctx){
    .term_width = 40,
    .term_height = 40,
    .is_tty = false,
    .supports_ansi = false,
    .colour_enabled = false,
  };

  ctx->is_tty = (isatty(STDOUT_FILENO) == 1);

  /* standard output is not in a tty, so return -1
   * TODO should probably check if stderr is a tty too */
  if (!ctx->is_tty) {
    log_info("Standard output is not a TTY.");
    return 0;
  }

  ctx->supports_ansi = (ctx->is_tty && !env_term_is_dumb());
  ctx->colour_enabled = (ctx->supports_ansi && !env_nocolor_defined());

  if (!try_get_terminal_dimensions(STDOUT_FILENO, &ctx->term_width, &ctx->term_height)) {
    log_info("Failed to retrieve terminal dimensions, using default.");
  }

  return 0;
}
