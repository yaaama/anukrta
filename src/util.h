#ifndef ANU_UTIL_H
#define ANU_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 1 byte (8 bits) boolean type */
typedef int8_t b8;
/* 4 byte (32 bits) boolean type */
typedef int32_t b32;
typedef uint8_t u8;
typedef int32_t i32;
typedef int64_t i64;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef uint8_t byte;
typedef uintptr_t uptr;
typedef ptrdiff_t size;
typedef size_t usize;

#ifndef CACHE_LINE_SIZE
/* Apple Silicon (M1/M2/M3) uses 128-byte cache lines */
#  if defined(__APPLE__) && defined(__aarch64__)
#    define CACHE_LINE_SIZE 128
/* IBM PowerPC and mainframes also often use 128 */
#  elif defined(__powerpc__) || defined(__s390x__)
#    define CACHE_LINE_SIZE 128
/* x86, x86_64, and standard ARM all use 64 */
#  else
#    define CACHE_LINE_SIZE 64
#  endif
#endif

/**
 * @brief One second (s) in microseconds (us)
 *
 * This is useful as FFmpeg uses microseconds for their internal timebase.
 **/
#define ANU_TIME_ONE_SEC_IN_US 1000000ULL

/**
 * @brief Converts microseconds to seconds.
 * @param microseconds The value in us.
 * @return The equivalent value in seconds.
 */
static inline double anu_time_microseconds_to_seconds (size_t microseconds) {
  return (microseconds > 0) ? ((double) microseconds / ANU_TIME_ONE_SEC_IN_US)
                            : 0;
}

/**
 * @brief Converts seconds to microseconds.
 * @param seconds The value in decimal seconds.
 * @return The equivalent value in microseconds.
 */
static inline size_t anu_time_seconds_to_microseconds (double seconds) {
  return (seconds > 0) ? (size_t) (seconds * (double) ANU_TIME_ONE_SEC_IN_US)
                       : 0;
}

/** @} */  // END TIME

/**
 * @brief Array size macro
 **/
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/** @name Number related macros
 * Macros to help with numbers
 * @{
 */

/**
 * @brief Round up 32 bit integer variable to next power of 2.
 **/
#define ROUNDUP_32(x)                                                         \
  (--(x), (x) |= (x) >> 1, (x) |= (x) >> 2, (x) |= (x) >> 4, (x) |= (x) >> 8, \
   (x) |= (x) >> 16, ++(x))

/**
 * @brief Round up 64 bit integer variable to next power of 2.
 **/
#define ROUNDUP_64(x)                                                         \
  (--(x), (x) |= (x) >> 1, (x) |= (x) >> 2, (x) |= (x) >> 4, (x) |= (x) >> 8, \
   (x) |= (x) >> 16, (x) |= (x) >> 32, ++(x))

/**
 *  @brief Return larger value
 **/
#define MAXIMUM(x, y) ((x) > (y) ? (x) : (y))

/**
 * @brief Return smaller value
 **/
#define MINIMUM(x, y) ((x) < (y) ? (x) : (y))

/**
 * @brief Absolute value of x
 */
#define ABSOLUTE(X) (X) > 0) ? (X) : -(X))

/**
 * @brief Difference of x and y
 **/
#define DIFF(A, B) ((A) > (B) ? (A) - (B) : (B) - (A))

/**
 * @brief Range constraint macro to ensure value is between min and max.
 **/
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM((_val), (_max)), (_min))

#define KILOBYTE(bytes) ((bytes) * 1000ULL)
#define MEGABYTE(bytes) (KILOBYTE(bytes) * 1000ULL)
#define GIGABYTE(bytes) (MEGABYTE(bytes) * 1000ULL)
#define TERABYTE(bytes) (GIGABYTE(bytes) * 1000ULL)

#define KIBIBYTE(bytes) ((bytes) * 1024ULL)
#define MEBIBYTE(bytes) (KIBIBYTE(bytes) * 1024ULL)
#define GIBIBYTE(bytes) (MEBIBYTE(bytes) * 1024ULL)
#define TEBIBYTE(bytes) (GIBIBYTE(bytes) * 1024ULL)
/** @} */

static inline int hamming_distance (uint64_t hash1, uint64_t hash2) {

  static_assert(
      sizeof(unsigned long long) >= 8,
      "Unsigned long longs must be 64 bits for this implementation to work.");

  return __builtin_popcountll(hash1 ^ hash2);
}

void print_matrix_float(FILE *fd, const float *matrix, int rows, int cols);
void anu_util_print_indent(FILE *fd, int spaces, int depth);

static inline int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

#define ZERO_MEMORY(pointer, count, type) \
  memset((pointer), 0, (count) * sizeof(type))

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define ALWAYS_INLINE inline __attribute__((always_inline))
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

#ifdef ANU_DEBUG  // If its in DEBUG MODE

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

/* ------------------------------------------------------------------------ */
#else
/* Optimise unreachable code away when in release builds. */
#  define UNREACHABLE(message) __builtin_unreachable()

/* Tell compiler our assumptions are TRUE and optimise out anything contrary. */
#  define ASSUME(cond) \
    do {               \
      if (!(cond))     \
        UNREACHABLE(); \
    } while (0)

#endif  // UNREACHABLE

#endif  // ANU_UTIL_H
