#include "signals.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

/**
 * Watcher thread: sleeps in sigwait() until SIGINT/SIGTERM arrives, records
 * it, runs the registered callback, then sets up hard kill (if sigint/sigterm is sent again).
 *
 * @param arg Pointer to the ak_signal_ctx.
 */
static void *signal_watch_thread (void *arg) {
  ak_signals_ctx *sig = arg;

  /* kill signals we wait on. Must match what ak_signals_install() blocked. */
  sigset_t kill_set;
  sigemptyset(&kill_set);
  sigaddset(&kill_set, SIGINT);
  sigaddset(&kill_set, SIGTERM);

  int signo = 0;

  /* Sleep until a shutdown signal arrives. The signals are blocked in every
   * thread, so instead of killing the process they'll hang around until consumed here. */
  if (sigwait(&kill_set, &signo) != 0) {
    log_error("sigwait() failed; shutdown signals will not be handled.");
    return NULL;
  }

  /* Record the signal FIRST, so ak_shutdown_requested() reports true even
   * if the callback below is slow. */
  atomic_store_explicit(&sig->received, signo, memory_order_release);

  /* Second press = hard kill. Restore the default behaviour (dying).
   * Do this before the callback as the callback may take time. */
  struct sigaction sa = {0};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  /* Unblock both SIGINT/SIGTERM signal in THIS thread only.
   * The signal mask is per-thread, so the next process-directed signal is delivered here and
   * kills the process immediately */
  pthread_sigmask(SIG_UNBLOCK, &kill_set, NULL);

  /* Let the user know we are exiting... */
  fprintf(stderr, "\n\n[anukrta] Interrupted (%s), shutting down...\n",
          signo == SIGINT ? "ctrl+c" : "SIGTERM");

  /* Notify whoever registered (e.g. poison the hashing work queue).
   * NOTE: NULL is possible: a signal may arrive before any phase registered. */
  ak_signal_callback_fn cb = atomic_load_explicit(&sig->on_shutdown, memory_order_acquire);
  if (cb) {
    cb(signo, sig->on_shutdown_data);
  }

  return NULL;
}

int ak_signals_install (ak_signals_ctx *ctx) {

  atomic_init(&ctx->received, 0);
  atomic_init(&ctx->on_shutdown, NULL);
  ctx->on_shutdown_data = NULL;

  /* Block the signals we intend to handle ourselves */
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);

  /* SIGWINCH is blocked defensively;
   * we don't actually handle it but we poll for it in `ak_term_ctx_update()`. */
  sigaddset(&set, SIGWINCH);

  if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
    return -1;
  }

  pthread_t watcher;
  if (pthread_create(&watcher, NULL, signal_watch_thread, ctx) != 0) {
    return -1;
  }

  /* Detached the thread
   * It sleeps in sigwait() for the program's entire lifetime and
   * dies when the process dies. */
  pthread_detach(watcher);
  return 0;
}

bool ak_shutdown_requested (const ak_signals_ctx *ctx) {
  return atomic_load_explicit(&ctx->received, memory_order_acquire) != 0;
}

int ak_shutdown_exit_code (const ak_signals_ctx *ctx) {
  int signo = atomic_load_explicit(&ctx->received, memory_order_acquire);
  return signo ? 128 + signo : 0; /* 130 = SIGINT, 143 = SIGTERM */
}
