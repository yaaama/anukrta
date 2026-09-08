#ifndef UI_H_
#define UI_H_
#include <stdatomic.h>
#include <stddef.h>
#include <time.h>

#include "config.h"
#include "term.h"
#include "util.h"

/**
 * High-level progress bar state.
 */
typedef struct anu_ui_ctx {
  struct timespec start_time;
  anu_term_ctx *term;
  char *label;
  pthread_t monitor_thread;
  atomic_size_t *completed_count;
  size_t total_count;
  atomic_bool is_active;
  bool is_interactive;
} anu_ui_ctx;

/**
 * @brief Allocate and initialize the UI context.
 *
 * @param config Pointer to global runtime configuration.
 * @param term Pointer to terminal struct context
 * @return Pointer to allocated UI context, or NULL on failure.
 */
int anu_ui_init(const anu_config *config, anu_term_ctx *term, anu_ui_ctx *ui);

/**
 * Destroy UI context, restore terminal state, and free memory.
 */
void anu_ui_destroy(anu_ui_ctx *ctx);

DEFINE_FREE(anu_ui, anu_ui_ctx *, if (_T) anu_ui_destroy(_T))

/**
 * Start the progress bar monitor thread.
 *
 * @param ctx UI context.
 * @param completed_count Pointer to atomic counter incremented by workers.
 * @param total_count Total number of items to process.
 * @param label Task name/description to display next to the bar.
 * @return 0 on success, negative error code on failure.
 */
int anu_ui_start_progress(anu_ui_ctx *ctx,
                          atomic_size_t *completed_count,
                          size_t total_count,
                          const char *label,
                          int label_len);

/**
 * Stop the progress monitor thread and clear the progress bar from terminal.
 *
 * @param ctx UI context.
 */
void anu_ui_stop_progress(anu_ui_ctx *ctx);

#endif  // UI_H_
