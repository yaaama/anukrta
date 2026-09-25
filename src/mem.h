#ifndef AK_MEM_H_
#define AK_MEM_H_

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/* NOLINTBEGIN (bugprone-unsafe-functions) */

#ifndef XALLOC_EXIT_CODE
#  define XALLOC_EXIT_CODE EXIT_FAILURE
#endif

static AK_NEVER_INLINE AK_COLD_FUNC AK_NO_RETURN void ak__err_oom (const char *file, unsigned int line) {
  fprintf(stderr, "%s:%u Out of memory! Cannot continue.\n", file, line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

static AK_NEVER_INLINE AK_COLD_FUNC AK_NO_RETURN void ak__err_alloc_sz_zero (const char *file,
                                                                             unsigned int line) {
  fprintf(stderr, "%s:%u Attempting to make allocation with size 0!\n", file, line);
  exit(XALLOC_EXIT_CODE);  // NOLINT (concurrency-mt-unsafe)
}

static AK_ALWAYS_INLINE AK_ALLOC_SZ (1)
    AK_NO_DISCARD void *ak__xmalloc_impl(const size_t sz, const char *filename, const unsigned int line) {

  if (ak_unlikely(sz == 0)) {
    ak__err_alloc_sz_zero(filename, line);
  }

  void *ret = malloc(sz);

  if (ak_unlikely(!ret && sz)) {
    ak__err_oom(filename, line);
  }
  return ret;
}

static AK_ALWAYS_INLINE AK_ALLOC_SZ (1, 2) AK_NO_DISCARD void *ak__xcalloc_impl(const size_t nmem,
                                                                                const size_t sz,
                                                                                const char *filename,
                                                                                const unsigned int line) {

  void *ret = calloc(nmem, sz);

  if (ak_unlikely(!ret && sz && nmem)) {
    ak__err_oom(filename, line);
  }

  return ret;
}

static AK_ALWAYS_INLINE AK_ALLOC_SZ (2) AK_NO_DISCARD void *ak__xrealloc_impl(void *ptr,
                                                                              const size_t sz,
                                                                              const char *filename,
                                                                              const unsigned int line) {

  if (ak_unlikely(sz == 0)) {
    ak__err_alloc_sz_zero(filename, line);
  }

  void *ret = realloc(ptr, sz);

  if (ak_unlikely(!ret && sz)) {
    ak__err_oom(filename, line);
  }
  return ret;
}

#define xmalloc(sz) ak__xmalloc_impl((sz), __FILE__, __LINE__)
#define xcalloc(nmem, sz) ak__xcalloc_impl((nmem), (sz), __FILE__, __LINE__)
#define xrealloc(ptr, sz) ak__xrealloc_impl((ptr), (sz), __FILE__, __LINE__)
#define xtcalloc(type, nmem) xcalloc((nmem), sizeof(type))

/* NOLINTEND */

#endif  // AK_MEM_H_
