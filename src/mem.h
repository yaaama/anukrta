#ifndef MEM_H_
#define MEM_H_

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/* NOLINTBEGIN (bugprone-unsafe-functions) */

#ifndef XALLOC_EXIT_CODE
#  define XALLOC_EXIT_CODE EXIT_FAILURE
#endif

static AK_NEVER_INLINE AK_COLD_FUNC AK_NO_RETURN void __err_oom (const char *file, unsigned int line) {
  fprintf(stderr, "%s:%d Out of memory! Cannot continue.\n", file, line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

static AK_NEVER_INLINE AK_COLD_FUNC AK_NO_RETURN void __err_alloc_sz_zero (const char *file,
                                                                           unsigned int line) {
  fprintf(stderr, "%s:%d Attempting to make allocation with size 0!\n", file, line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

#define err_oom() __err_oom(__FILE__, __LINE__)

static inline AK_ALLOC_SZ(1) AK_NO_DISCARD void *xmalloc(const size_t sz) {
  void *ret = malloc(sz);
  if (ak_unlikely(!ret && sz)) {
    err_oom();
  }
  return ret;
}

static inline AK_ALLOC_SZ(2) AK_NO_DISCARD void *xaligned_alloc(const size_t alignment, const size_t sz) {
  void *ret = aligned_alloc(alignment, sz);
  if (ak_unlikely(!ret && sz && alignment)) {
    err_oom();
  }
  return ret;
}

static inline AK_ALLOC_SZ(1, 2) AK_NO_DISCARD void *xcalloc(const size_t nmem, const size_t sz) {

  void *ret = calloc(nmem, sz);
  if (ak_unlikely(!ret && sz && nmem)) {
    err_oom();
  }
  return ret;
}

static inline AK_ALLOC_SZ(2) AK_NO_DISCARD void *xrealloc(void *ptr, const size_t sz) {

  if (ak_unlikely(sz == 0)) {
    __err_alloc_sz_zero(__FILE__, __LINE__);
  }
  void *ret = realloc(ptr, sz);

  if (ak_unlikely(!ret && sz)) {
    err_oom();
  }
  return ret;
}

#define xtcalloc(type, nmem) xcalloc(nmem, sizeof(type))

/* NOLINTEND */

#endif  // MEM_H_
