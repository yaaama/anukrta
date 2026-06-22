#ifndef MEM_H_
#define MEM_H_

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

#ifndef XALLOC_EXIT_CODE
#  define XALLOC_EXIT_CODE EXIT_FAILURE
#endif

static __attribute__((__noreturn__, __cold__, noinline)) void __err_oom (
    const char *file,
    unsigned int line) {
  fprintf(stderr, "%s:%d Out of memory! Cannot continue.\n", file, line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

static __attribute__((__noreturn__, __cold__, noinline)) void
__err_alloc_sz_zero (const char *file, unsigned int line) {
  fprintf(stderr, "%s:%d Attempting to make allocation with size 0!\n", file,
          line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

#define err_oom() __err_oom(__FILE__, __LINE__)

static inline FUNC_ALLOC_SIZE(1) MUST_CHECK void *xmalloc(const size_t size) {
  void *ret = malloc(size);
  if (UNLIKELY(!ret && size)) {
    err_oom();
  }
  return ret;
}

static inline FUNC_ALLOC_SIZE(1, 2) MUST_CHECK
    void *xcalloc(const size_t nmem, const size_t size) {

  void *ret = calloc(nmem, size);
  if (UNLIKELY(!ret && size && nmem)) {
    err_oom();
  }
  return ret;
}

static inline FUNC_ALLOC_SIZE(2) MUST_CHECK
    void *xrealloc(void *ptr, const size_t size) {

  if (UNLIKELY(size == 0)) {
    __err_alloc_sz_zero(__FILE__, __LINE__);
  }
  void *ret = realloc(ptr, size);

  if (UNLIKELY(!ret && size)) {
    err_oom();
  }
  return ret;
}

#define xtcalloc(type, nmem) xcalloc(nmem, sizeof(type))

#endif  // MEM_H_
