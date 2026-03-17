#ifndef ANU_UTIL_H
#define ANU_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 1 byte (8 bits) boolean type */
typedef int8_t b8;
/* 4 byte (32 bits) boolean type */
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

typedef enum anu_hash_type {
  ANU_HASH_ALGO_AVERAGE = 0,
  ANU_HASH_ALGO_DCT = 1,
} anu_hash_type;

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
  b32 detect_black_frames; /* TODO Detect black frames in video and skip them? */
  b32 detect_rotation;     /* TODO Detect rotation in videos? */
  b32 detect_bars;         /* Detect windowboxing/letterboxing/pillarboxing */
  anu_hash_type hash_algorithm; /* Hashing algorithm to use */
} anukrta_config;

/**
 * @brief One second (s) in microseconds (us)
 *
 * This is useful as FFmpeg uses microseconds for their timebase
 **/
#define ANU_TIME_ONE_SEC_IN_US 1000000

/**
 * @brief Array size macro
 **/
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/**
 *  @brief Return larger value
 **/
#define MAXIMUM(x, y) ((x) > (y) ? (x) : (y))

/**
 * @brief Return smaller value
 **/
#define MINIMUM(x, y) ((x) < (y) ? (x) : (y))

/**
 * @brief Range constraint macro to ensure value is between min and max.
 **/
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM((_val), (_max)), (_min))

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

static inline int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

static inline double anu_time_microseconds_to_seconds (size_t microseconds) {
  return ((double) microseconds / ANU_TIME_ONE_SEC_IN_US);
}

static inline size_t anu_time_seconds_to_microseconds (double seconds) {
  return (seconds > 0) ? (size_t) (seconds * (double) ANU_TIME_ONE_SEC_IN_US)
                       : 0;
}

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define HOT_FUNCTION __attribute__((hot))

#if defined(__GNUC__) || defined(__clang__)
#  define MAYBE_UNUSED __attribute__((unused))
#else
#  define MAYBE_UNUSED
#endif

#define STRINGIFY(s) TOSTRING(s)
#define TOSTRING(s) #s

#define GLUE(a, b) a##b
#define JOIN(a, b) GLUE(a, b)

/**
 *  @brief Print panic message and abort the program as our code is broken.
 *
 * To be used only when there is some logical issue in our code.
 **/
#define ANU_PANIC(message)                                             \
  do {                                                                 \
    (void) fprintf(stderr, "[PANIC]: %s:%d: %s\n", __FILE__, __LINE__, \
                   (message));                                         \
    abort();                                                           \
  } while (0)

/**
 *  @brief Print message and exit as we have encountered external error.
 *
 *  Used when we encounter issues such as memory allocation failure.
 **/
#define ANU_DIE(message)                                               \
  do {                                                                 \
    (void) fprintf(stderr, "[FATAL]: %s:%d: %s\n", __FILE__, __LINE__, \
                   (message));                                         \
    (void) fflush(stderr);                                             \
    abort();                                                           \
  } while (0)

/**
 *  @brief Print message and exit, as this section of code is not implemented yet.
 **/
#define TODO(message)                                               \
  do {                                                              \
    (void) fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, \
                   (message));                                      \
    (void) fflush(stderr);                                          \
    abort();                                                        \
  } while (0)

#ifdef NDEBUG  // UNREACHABLE
/* Optimise unreachable code away when in release builds. */
#  define UNREACHABLE(message) __builtin_unreachable()

/* Tell compiler our assumptions are TRUE and optimise out anything contrary. */
#  define ASSUME(cond) \
    do {               \
      if (!(cond))     \
        UNREACHABLE(); \
    } while (0)

#else
/* Debug builds should crash when reaching unreachable code. */
#  define UNREACHABLE(message)                                           \
    do {                                                                 \
      (void) fprintf(stderr, "Unreachable code reached at: %s:%d: %s\n", \
                     __FILE__, __LINE__, (message));                     \
      abort();                                                           \
    } while (0)

/* Assumption crashes when false. */
#  define ASSUME(cond)                                                   \
    do {                                                                 \
      if (!(cond)) {                                                     \
        (void) fprintf(stderr, "[PANIC] Assertion %s failed at %s:%d\n", \
                       STRINGIFY(cond), __FILE__, __LINE__);             \
        abort();                                                         \
      }                                                                  \
    } while (0)

#endif  // UNREACHABLE

#endif  // ANU_UTIL_H
