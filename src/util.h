#ifndef ANU_UTIL_H
#define ANU_UTIL_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef int32_t b32;
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
  b32 verbose;             /* Turn on verbose output */
  b32 dry_run;             /* TODO Do not save/actually process any files */
  b32 detect_black_frames; /* Detect black frames in video and skip them? */
  b32 detect_rotation;     /* Detect rotation in videos? */
  int segments;            /* Number of segments to hash from a video */
  int threshold;      /* Threshold of similarity to consider them duplicates */
  int hash_algorithm; /* Hashing algorithm to use */
  long skip_duration; /* Video length shorter than this will be skipped */
  b32 scan_curr_dir;
  b32 _exit_early;
  i32 thread_count;
  i32 paths_count;
  char **paths;
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

#define ANU_DIE(msg)                                           \
  do {                                                         \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, (msg)); \
    __builtin_unreachable();                                   \
  } while (0);

#define TODO(message)                                                    \
  do {                                                                   \
    fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, (message)); \
    __builtin_unreachable();                                             \
  } while (0)

#define UNREACHABLE(message)                                           \
  do {                                                                 \
    fprintf(stderr, "Unreachable Code Reached: %s:%d: %s\n", __FILE__, \
            __LINE__, (message));                                      \
    __builtin_unreachable();                                           \
  } while (0)

#define KILOBYTE(x) ((x) * 1000ULL)
#define MEGABYTE(x) (KILOBYTE(x) * 1000ULL)
#define GIGABYTE(x) (MEGABYTE(x) * 1000ULL)
#define TERABYTE(x) (GIGABYTE(x) * 1000ULL)

#define KIBIBYTE(x) ((x) * 1024ULL)
#define MEBIBYTE(x) (KIBIBYTE(x) * 1024ULL)
#define GIBIBYTE(x) (MEBIBYTE(x) * 1024ULL)
#define TEBIBYTE(x) (TEBIBYTE(x) * 1024ULL)

i32 hamming_distance(uint64_t hash1, uint64_t hash2);
void debug_print_matrix(const float *matrix, int rows, int cols);
void anu_util_print_indent(int depth);

ALWAYS_INLINE int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

ALWAYS_INLINE double anu_time_microseconds_to_seconds (long microseconds) {
  return ((double) microseconds / ANU_TIME_ONE_SEC_IN_US);
}

ALWAYS_INLINE long anu_time_seconds_to_microseconds (double seconds) {
  return (long) (seconds * (double) ANU_TIME_ONE_SEC_IN_US);
}

#endif  // ANU_UTIL_H
