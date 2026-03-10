#ifndef ANU_UTIL_H
#define ANU_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hash.h"

/* Boolean 1 byte */
typedef int8_t b8;
/* Boolean 4 bytes */
typedef int32_t b32;
typedef uint8_t u8;
typedef int32_t i32;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef uint8_t byte;
typedef uintptr_t uptr;
typedef ptrdiff_t size;
typedef size_t usize;

/* Default thread count */
#define ANU_DEF_THREAD_COUNT 8
#define ANU_MAX_PATH_LEN 512

/* Structure describing the configuration settings to use for this run. */
typedef struct anukrta_config {
  char **paths;
  size_t skip_duration; /* Video length shorter than this will be skipped */
  size_t paths_count;   /* Number of paths we parsed from cli args */
  size_t threshold;     /* Similarity threshold */
  size_t segments;      /* Number of segments to hash from a video */
  size_t thread_count;  /* Number of threads */
  b32 verbose;          /* Turn on verbose output */
  b32 scan_curr_dir;    /* Only scan current directory */
  b32 _exit_early;      /* Exit quickly (set when we parse '-h' for example) */
  b32 dry_run;          /* TODO Do not save/actually process any files */
  b32 detect_black_frames; /* Detect black frames in video and skip them? */
  b32 detect_rotation;     /* Detect rotation in videos? */
  anu_hash_type hash_algorithm; /* Hashing algorithm to use */
} anukrta_config;

/**
 * @brief
 * One second in microseconds
 *
 * This is useful as FFmpeg uses microseconds for their timebase
 */
#define ANU_TIME_ONE_SEC_IN_US 1000000

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define HOT_FUNCTION __attribute__((hot))

#define STRINGIFY(s) TOSTRING(s)
#define TOSTRING(s) #s

#define GLUE(a, b) a##b
#define JOIN(a, b) GLUE(a, b)

/* Array size macro */
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/* Return bigger value */
#define MAXIMUM(x, y) ((x) > (y) ? (x) : (y))
/* Return smaller value */
#define MINIMUM(x, y) ((x) < (y) ? (x) : (y))
/* Range constraint macro to ensure value is between min and max */
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM(_val, _max), _min)

#define ANU_DIE(message)                                                  \
  do {                                                                    \
    (void) fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, (message)); \
    abort();                                                              \
  } while (0);

#define TODO(message)                                               \
  do {                                                              \
    (void) fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, \
                   (message));                                      \
    abort();                                                        \
  } while (0)

#define UNREACHABLE(message)                                               \
  do {                                                                     \
    (void) fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, \
                   (message));                                             \
    abort();                                                               \
  } while (0)

#define KILOBYTE(x) ((x) * 1000ULL)
#define MEGABYTE(x) (KILOBYTE(x) * 1000ULL)
#define GIGABYTE(x) (MEGABYTE(x) * 1000ULL)
#define TERABYTE(x) (GIGABYTE(x) * 1000ULL)

#define KIBIBYTE(x) ((x) * 1024ULL)
#define MEBIBYTE(x) (KIBIBYTE(x) * 1024ULL)
#define GIBIBYTE(x) (MEBIBYTE(x) * 1024ULL)
#define TEBIBYTE(x) (TEBIBYTE(x) * 1024ULL)

int hamming_distance(uint64_t hash1, uint64_t hash2);
void debug_print_matrix(const float *matrix, int rows, int cols);
void anu_util_print_indent(int depth);

ALWAYS_INLINE int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

ALWAYS_INLINE double anu_time_microseconds_to_seconds (size_t microseconds) {
  return ((double) microseconds / ANU_TIME_ONE_SEC_IN_US);
}

ALWAYS_INLINE size_t anu_time_seconds_to_microseconds (double seconds) {
  return (seconds > 0) ? (size_t) (seconds * (double) ANU_TIME_ONE_SEC_IN_US)
                       : 0;
}

#endif  // ANU_UTIL_H
