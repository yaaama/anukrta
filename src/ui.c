#include "ui.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "log.h"
#include "term.h"
#include "util.h"

static double get_elapsed_seconds (const struct timespec *start, const struct timespec *end) {
  return (double) (end->tv_sec - start->tv_sec) + ((double) (end->tv_nsec - start->tv_nsec) / 1000000000.0);
}

static void render_progress_bar (ak_ui_ctx *ctx, size_t completed, double elapsed_sec) {
  if (!ctx->is_active) {
    return;
  }

  int term_width = ctx->term->term_width;
  if (term_width < 20) {
    term_width = 20;
  }

  double progress = 0.0;
  if (ctx->total_count > 0) {
    progress = (double) completed / (double) ctx->total_count;
  }
  progress = CLAMP_BETWEEN(progress, 0.0, 1.0);

  /* Calculate throughput and ETA */
  double items_per_sec = (elapsed_sec > 0.05) ? ((double) completed / elapsed_sec) : 0.0;
  int eta_sec = 0;
  if (items_per_sec > 0.001 && completed < ctx->total_count) {
    eta_sec = (int) ceil((double) (ctx->total_count - completed) / items_per_sec);
  }

  char stats_buf[128];
  int eta_m = eta_sec / 60;
  int eta_s = eta_sec % 60;

  if (completed >= ctx->total_count) {
    snprintf(stats_buf, sizeof(stats_buf), " %zu/%zu (100%%) in %.1fs", completed, ctx->total_count,
             elapsed_sec);
  } else if (items_per_sec > 0.0) {
    snprintf(stats_buf, sizeof(stats_buf), " %zu/%zu (%.1f%%) [ETA %02d:%02d, %.1f f/s]", completed,
             ctx->total_count, progress * 100.0, eta_m, eta_s, items_per_sec);
  } else {
    snprintf(stats_buf, sizeof(stats_buf), " %zu/%zu (%.1f%%)", completed, ctx->total_count,
             progress * 100.0);
  }

  /* Compute available space for the bracketed bar [===>   ] */
  int label_len = (int) strlen(ctx->label);
  int stats_len = (int) strlen(stats_buf);
  int fixed_overhead = label_len + stats_len + 4 + 10; /* label + ": [" + stats */
  int bar_width = term_width - fixed_overhead;

  /* If the terminal is narrow, suppress the bracketed bar and only show statistics */
  if (bar_width < 5) {
    ak_term_clear_line(stdout);
    printf("%s: %s", ctx->label, stats_buf);
    fflush(stdout);
    return;
  }

  int filled = (int) (progress * (double) bar_width);

  ak_term_clear_line(stdout);
  printf("%s: [", ctx->label);
  for (int i = 0; i < bar_width; i++) {
    if (i < filled) {
      putchar('=');
    } else if (i == filled) {
      putchar('>');
    } else {
      putchar(' ');
    }
  }
  printf("]%s", stats_buf);
  fflush(stdout);
}

static void *progress_monitor_thread (void *arg) {
  ak_ui_ctx *ctx = (ak_ui_ctx *) arg;

  size_t last_completed = SIZE_MAX;
  i64 last_elapsed_sec = -1;

  while (atomic_load(&ctx->is_active)) {
    size_t completed = atomic_load(ctx->completed_count);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = get_elapsed_seconds(&ctx->start_time, &now);
    i64 current_elapsed_sec = (i64) elapsed;
    bool resized = ak_term_ctx_update(ctx->term); /* poll every tick */
    /* Only render if the count increased or if a whole second has passed (so ETA timer updates) */
    if (completed != last_completed || current_elapsed_sec != last_elapsed_sec || resized) {
      render_progress_bar(ctx, completed, elapsed);
      last_completed = completed;
      last_elapsed_sec = current_elapsed_sec;
    }

    if (completed >= ctx->total_count) {
      break;
    }

    /* 150ms sleep (approx 6.6 FPS) avoids high CPU overhead */
    struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 150000000};
    nanosleep(&sleep_time, NULL);
  }

  return NULL;
}

void ak_ui_destroy (ak_ui_ctx *ctx) {
  if (!ctx) {
    return;
  }

  if (atomic_load(&ctx->is_active)) {
    ak_ui_progress_stop(ctx);
  }

  if (ctx->label) {
    free(ctx->label);
  }

  free(ctx);
}

int ak_ui_ctx_init (const ak_config *config, ak_term_ctx *term, ak_ui_ctx *ui_ctx) {

  /* Enable interactive progress bar only if:
   * - Is not explicitly disabled
   * - Output is an interactive TTY
   * - Not a dumb terminal
   * - Terminal was initialized properly
   * - Verbosity level is 0 (verbosity > 0 will conflict with progress output) */

  ui_ctx->term = term;
  ui_ctx->label = NULL;
  ui_ctx->completed_count = NULL;
  ui_ctx->total_count = 0;

  /* Enable progress bar only if interactive, supported, and verbosity == 0 */
  bool flag_enabled = ak_flag_has(config->runtime_flags, RT_PROGRESS_BAR);
  bool can_render = (term->is_tty && term->supports_ansi);
  bool quiet = (ak_get_verbosity(config->runtime_flags) == 0);

  ui_ctx->is_interactive = (flag_enabled && can_render && quiet);
  atomic_store(&ui_ctx->is_active, false);
  return 0;
}

int ak_ui_progress_start (ak_ui_ctx *ctx,
                          atomic_size_t *completed_count,
                          size_t total_count,
                          const char *label,
                          int label_len) {

  if (!ctx || !completed_count || total_count == 0) {
    return -1;
  }

  /* If is not interactive terminal, then return */
  if (!ctx->is_interactive) {
    return 0;
  }

  ak_term_cursor_hide(stdout);

  ctx->completed_count = completed_count;
  ctx->total_count = total_count;
  ctx->label = strdup(label);

  clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
  atomic_store(&ctx->is_active, true);
  if (pthread_create(&ctx->monitor_thread, NULL, progress_monitor_thread, ctx) != 0) {
    log_error("Failed to create UI progress monitor thread");
    atomic_store(&ctx->is_active, false);
    ak_term_cursor_show(stdout);
    return -1;
  }
  return 0;
}

void ak_ui_progress_stop (ak_ui_ctx *ctx) {
  if (!ctx) {
    return;
  }
  if (!atomic_load(&ctx->is_active)) {
    return;
  }

  atomic_store(&ctx->is_active, false);
  if (ctx->monitor_thread) {
    pthread_join(ctx->monitor_thread, NULL);
  }

  /* Clear the progress bar */
  if (ctx->is_interactive) {
    ak_term_clear_line(stdout);
    ak_term_cursor_show(stdout);
    printf("\n");
    fflush(stdout);
  }
}
