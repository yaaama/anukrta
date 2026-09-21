#include "term.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "log.h"
#include "mem.h"

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

static int init_sigwinch_handling (ak_term_ctx *ctx) {
  /* Create a signal set containing ONLY SIGWINCH */
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGWINCH);

  ctx->old_mask = xcalloc(1, sizeof(*ctx->old_mask));
  /* Block SIGWINCH signal */
  if (pthread_sigmask(SIG_BLOCK, &mask, ctx->old_mask) != 0) {
    log_error("Failed to block signals!");
    return -1;
  }

  /* Store FD of signal queue */
  ctx->signals_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (ctx->signals_fd == -1) {
    log_error("Failed to create signalfd");
    return -1;
  }

  /* Create epoll */
  ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (ctx->epoll_fd == -1) {
    log_error("Failed to create epoll instance");
    return -1;
  }

  /* Register with epoll */
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = ctx->signals_fd};
  if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->signals_fd, &ev) == -1) {
    log_error("Failed to add signalfd to epoll");
    return -1;
  }
  return 0;
}

int ak_term_ctx_update (ak_term_ctx *ctx) {
  if (ctx->epoll_fd < 0 || ctx->signals_fd < 0) {
    return -1;
  }

  struct epoll_event ev;
  /* Non-blocking poll (timeout = 0) */
  int n = epoll_wait(ctx->epoll_fd, &ev, 1, 0);

  if (n == -1) {
    /* Interrupted by unblocked signal, safe to ignore */
    if (errno == EINTR) {
      return 0;
    }
    return -1;
  }

  if (n <= 0) {
    return 0;
  }

  struct signalfd_siginfo fdsi;
  ssize_t s;
  bool resized = false;

  /* Read all pending signals from the fd until EAGAIN */
  while ((s = read(ctx->signals_fd, &fdsi, sizeof(struct signalfd_siginfo))) > 0) {
    if (s == sizeof(struct signalfd_siginfo) && fdsi.ssi_signo == SIGWINCH) {
      resized = true;
    }
  }

  /* If a resize happened in this batch, update dimensions */
  if (resized) {
    try_get_terminal_dimensions(STDOUT_FILENO, &ctx->term_width, &ctx->term_height);
    return 1;
  }
  return 0;
}

void ak_term_ctx_destroy (ak_term_ctx *ctx) {
  if (!ctx) {
    return;
  }

  if (ctx->epoll_fd != -1) {
    close(ctx->epoll_fd);
    ctx->epoll_fd = -1;
  }

  if (ctx->signals_fd != -1) {
    close(ctx->signals_fd);
    ctx->signals_fd = -1;
  }

  /* Safely restore previous signal mask */
  pthread_sigmask(SIG_SETMASK, ctx->old_mask, NULL);

  free(ctx->old_mask);
  free(ctx);
}

int ak_term_ctx_init (ak_term_ctx *ctx) {

  /* Set defaults */
  *ctx = (ak_term_ctx){
    .term_width = 40,
    .term_height = 40,
    .signals_fd = -1,
    .epoll_fd = -1,
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

  /* Initialize the signal handling context */
  if (init_sigwinch_handling(ctx) == -1) {
    ak_term_ctx_destroy(ctx); /* Cleanup any partial allocation */
    return -1;
  }

  return 0;
}
