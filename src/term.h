#ifndef ANU_TERM_H_
#define ANU_TERM_H_

#include <stdio.h>

#include "util.h"

#define ANU_COLOR_RESET "\033[0m"
#define ANU_COLOR_BOLD "\033[1m"
#define ANU_COLOR_HALFBRIGHT "\033[2m"
#define ANU_COLOR_UNDERSCORE "\033[4m"
#define ANU_COLOR_BLINK "\033[5m"
#define ANU_COLOR_REVERSE "\033[7m"

/* Standard colors */
#define ANU_COLOR_BLACK "\033[30m"
#define ANU_COLOR_RED "\033[31m"
#define ANU_COLOR_GREEN "\033[32m"
#define ANU_COLOR_BROWN "\033[33m"
#define ANU_COLOR_BLUE "\033[34m"
#define ANU_COLOR_MAGENTA "\033[35m"
#define ANU_COLOR_CYAN "\033[36m"
#define ANU_COLOR_GRAY "\033[37m"

/* Bold variants */
#define ANU_COLOR_DARK_GRAY "\033[1;30m"
#define ANU_COLOR_BOLD_RED "\033[1;31m"
#define ANU_COLOR_BOLD_GREEN "\033[1;32m"
#define ANU_COLOR_BOLD_YELLOW "\033[1;33m"
#define ANU_COLOR_BOLD_BLUE "\033[1;34m"
#define ANU_COLOR_BOLD_MAGENTA "\033[1;35m"
#define ANU_COLOR_BOLD_CYAN "\033[1;36m"
#define ANU_COLOR_WHITE "\033[1;37m"

#define ANSI_CURSOR_HIDE "\033[?25l"
#define ANSI_CURSOR_SHOW "\033[?25h"
/* Erase characters until the end of the line */
#define ANSI_ERASE_TO_END_OF_LINE "\x1B[K"
/* Erase characters until end of screen */
#define ANSI_ERASE_TO_END_OF_SCREEN "\x1B[J"
/* Move cursor up one line */
#define ANSI_REVERSE_LINEFEED "\x1BM"

/* Set cursor to top left corner and clear screen */
#define ANSI_HOME_CLEAR "\x1B[H\x1B[2J"

/**
 * Current state of terminal (if there is one).
 */
typedef struct anu_term_ctx {
  /* Width of tty */
  int term_width;
  /* Height of terminal */
  int term_height;

  /* Window change signal file descriptor */
  int sigwinch_fd;
  /* File descriptor for epoll */
  int epoll_fd;

  /* TTY flags */
  bool is_tty;
  bool is_dumb;
} anu_term_ctx;

static ALWAYS_INLINE void anu_term_cursor_hide (FILE *stream) { fputs(ANSI_CURSOR_HIDE, stream); }

static ALWAYS_INLINE void anu_term_cursor_show (FILE *stream) { fputs(ANSI_CURSOR_SHOW, stream); }

static ALWAYS_INLINE void anu_term_clear_line (FILE *stream) {
  fputs("\r" ANSI_ERASE_TO_END_OF_LINE, stream);
}

/**
 * Initialise terminal context, block SIGWINCH signals and setup FDs.
 */
int anu_term_init(anu_term_ctx *ctx) _nonnull_(1);

/**
 * Close FDs and cleanup.
 */
void anu_term_destroy(anu_term_ctx *ctx);

DEFINE_FREE(anu_term_ctx, anu_term_ctx *, if (_T) anu_term_destroy(_T))

/**
 * Update terminal context
 */
int anu_term_update(anu_term_ctx *ctx);
#endif  // ANU_TERM_H_
