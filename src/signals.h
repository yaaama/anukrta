#ifndef AK_SIGNALS_H_
#define AK_SIGNALS_H_

#include <stdatomic.h>

typedef void (*ak_signal_callback_fn)(int signo, void *userdata);

typedef struct ak_signals_ctx {
  atomic_int received;                        /* 0 = running, else signo */
  _Atomic(ak_signal_callback_fn) on_shutdown; /* invoked on the signal thread */
  /** Opaque argument handed to `on_shutdown`. */
  void *on_shutdown_data;
} ak_signals_ctx;

/**
 * Block SIGINT/SIGTERM process-wide and spawn the watcher thread (detached).
 *
 * @param ctx Signal context.
 *
 * @warning Called EARLY in main(), BEFORE any other thread is created, so that
 * all later threads inherit the blocked signal mask.
 *
 * @return 0 on success, -1 on failure.
 */
int ak_signals_install(ak_signals_ctx *ctx);

/**
 * Check for whether shutdown signal (SIGINT/SIGTERM) has been received.
 */
bool ak_shutdown_requested(const ak_signals_ctx *ctx);

/**
 * Process exit code following shell convention: 128 + signo
 * (130 for SIGINT, 143 for SIGTERM). Returns 0 if no signal was received.
 */
int ak_shutdown_exit_code(const ak_signals_ctx *ctx);

#endif  // AK_SIGNALS_H_
