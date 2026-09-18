#ifndef AK_TERM_H_
#define AK_TERM_H_

#include <stdio.h>

#include "util.h"

#define AK_COLOR_RESET "\033[0m"
#define AK_COLOR_BOLD "\033[1m"
#define AK_COLOR_HALFBRIGHT "\033[2m"
#define AK_COLOR_UNDERSCORE "\033[4m"
#define AK_COLOR_BLINK "\033[5m"
#define AK_COLOR_REVERSE "\033[7m"

/* Standard colors */
#define AK_COLOR_BLACK "\033[30m"
#define AK_COLOR_RED "\033[31m"
#define AK_COLOR_GREEN "\033[32m"
#define AK_COLOR_BROWN "\033[33m"
#define AK_COLOR_BLUE "\033[34m"
#define AK_COLOR_MAGENTA "\033[35m"
#define AK_COLOR_CYAN "\033[36m"
#define AK_COLOR_GRAY "\033[37m"

/* Bold variants */
#define AK_COLOR_DARK_GRAY "\033[1;30m"
#define AK_COLOR_BOLD_RED "\033[1;31m"
#define AK_COLOR_BOLD_GREEN "\033[1;32m"
#define AK_COLOR_BOLD_YELLOW "\033[1;33m"
#define AK_COLOR_BOLD_BLUE "\033[1;34m"
#define AK_COLOR_BOLD_MAGENTA "\033[1;35m"
#define AK_COLOR_BOLD_CYAN "\033[1;36m"
#define AK_COLOR_WHITE "\033[1;37m"

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
typedef struct ak_term_ctx {
  int term_width;    /**< Width of terminal. */
  int term_height;   /**< Height of terminal. */
  int signals_fd;    /**< File descriptor where signals are written. */
  int epoll_fd;      /**< File descriptor for epoll. */
  bool is_tty;       /**< Is running in TTY. */
  bool is_dumb;      /**< Terminal is DUMB (no colour, or no escape processing etc). */
  bool quit_request; /**< Received a SIGINT/SIGTERM signal. */
} ak_term_ctx;

static AK_ALWAYS_INLINE void ak_term_cursor_hide (FILE *stream) { fputs(ANSI_CURSOR_HIDE, stream); }

static AK_ALWAYS_INLINE void ak_term_cursor_show (FILE *stream) { fputs(ANSI_CURSOR_SHOW, stream); }

static AK_ALWAYS_INLINE void ak_term_clear_line (FILE *stream) {
  fputs("\r" ANSI_ERASE_TO_END_OF_LINE, stream);
}

/**
 * Initialise terminal context, block SIGWINCH signals and setup FDs.
 */
int ak_term_ctx_init(ak_term_ctx *ctx) AK_NONNULL_ARG(1);

/**
 * Close FDs and cleanup terminal context.
 */
void ak_term_ctx_destroy(ak_term_ctx *ctx);

AK_DEFINE_AUTO(term_ctx, ak_term_ctx *, if (_T) ak_term_ctx_destroy(_T))

/**
 * Update terminal context
 */
int ak_term_ctx_update(ak_term_ctx *ctx);
#endif  // AK_TERM_H_
