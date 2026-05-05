/**
 * @file util.h
 * @brief Utility functions/macros used throughout the codebase.
 */

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

/**
 * @def CACHE_LINE_SIZE
 * @brief Number of bytes in a single cache line
 */
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
/* Number of datatypes that that fit within a single cache line */
#define CACHE_STRIDE_INT (CACHE_LINE_SIZE / sizeof(int))
#define CACHE_STRIDE_LONG (CACHE_LINE_SIZE / sizeof(long))
#define CACHE_STRIDE_LLONG (CACHE_LINE_SIZE / sizeof(long long))
#define CACHE_STRIDE_CHAR (CACHE_LINE_SIZE / sizeof(char))
#define CACHE_STRIDE_FLOAT (CACHE_LINE_SIZE / sizeof(float))
#define CACHE_STRIDE_DOUBLE (CACHE_LINE_SIZE / sizeof(double))

/** @name Time conversion utilities
 * Useful constants and inline functions to convert between different time bases.
 * @{
 */

/**
 * @brief One second (s) in microseconds (us)
 * This is useful as FFmpeg uses microseconds for their internal timebase.
 */
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

/** @name Math Related Macros
 * Macros to help with numbers and math
 * @{
 */

/** Return larger value */
#define MAXIMUM(X, Y) ((X) > (Y) ? (X) : (Y))

/** Return smallest number between x and y */
#define MINIMUM(X, Y) ((X) < (Y) ? (X) : (Y))

/** Absolute value of X */
#define ABSOLUTE(X) ((X) > 0 ? (X) : -(X))

/** Difference of x and y */
#define DIFF(A, B) ((A) > (B) ? (A) - (B) : (B) - (A))

/**
 * @brief Range constraint macro to ensure value is between min and max.
 * @param _val The value to clamp
 * @param _max Maximum value to clamp to
 * @param _min Mininmum value to clamp to
 * @return Clamped value
 */
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM((_val), (_max)), (_min))

/** Round up 32 bit integer variable to next power of 2. */
#define ROUNDUP_32(X)                                                         \
  (--(X), (X) |= (X) >> 1, (X) |= (X) >> 2, (X) |= (X) >> 4, (X) |= (X) >> 8, \
   (X) |= (X) >> 16, ++(X))

/** Round up 64 bit integer variable to next power of 2. */
#define ROUNDUP_64(X)                                                         \
  (--(X), (X) |= (X) >> 1, (X) |= (X) >> 2, (X) |= (X) >> 4, (X) |= (X) >> 8, \
   (X) |= (X) >> 16, (X) |= (X) >> 32, ++(X))

/** @} */  // END NUMBER

/** @name File Size Constants in Bytes
 * Math to convert size to their size in bytes
 * E.g. KILOBYTE(10) == 10,000 bytes
 * @{
 */

#define KILOBYTE(X) ((X) * 1000ULL)
#define MEGABYTE(X) (KILOBYTE(X) * 1000ULL)
#define GIGABYTE(X) (MEGABYTE(X) * 1000ULL)
#define TERABYTE(X) (GIGABYTE(X) * 1000ULL)

#define KIBIBYTE(X) ((X) * 1024ULL)
#define MEBIBYTE(X) (KIBIBYTE(X) * 1024ULL)
#define GIBIBYTE(X) (MEBIBYTE(X) * 1024ULL)
#define TEBIBYTE(X) (GIBIBYTE(X) * 1024ULL)

/** @} */

static_assert(
    sizeof(unsigned long long) >= 8,
    "Unsigned long longs must be at least 64 bits for our hamming distance "
    "implementation to work.");

/**
 * @brief Calculate hamming distance between two **unsigned** 64-bit integers.
 * Makes use of `__builtin_popcountll()`
 * @return Number of bits that differ between `X` and `Y` as an integer.
 * @retval 0 `X` and `Y` are the exact same.
 * @retval 64 `X` and `Y` are compliments of one another.
 * @retval k `X` and `Y` differ by `k` number of bits.
 **/
static inline int hamming_distance (const uint64_t a, const uint64_t b) {
  return __builtin_popcountll(a ^ b);
}

void print_matrix_float(FILE *fp, const float *matrix, int rows, int cols);

void anu_util_print_indent(FILE *fp, int spaces, int depth);

static inline int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

/** @brief Array size macro */
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/** @brief Zero out memory */
#define ZERO_MEMORY(pointer, count, type) \
  memset((pointer), 0, (count) * sizeof(type))

/** @brief Hint to compiler that the branch is most LIKELY true */
#define LIKELY(x) __builtin_expect(!!(x), 1)

/** @brief Hint to the compiler the condition is most likely FALSE */
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
 * @def ANU_PANIC
 * @brief Print panic message and abort the program as our code is broken.
 * @note To be used only when there is some logical issue in our code.
 */
#define ANU_PANIC(message)                                             \
  do {                                                                 \
    (void) fprintf(stderr, "[PANIC]: %s:%d: %s\n", __FILE__, __LINE__, \
                   (message));                                         \
    abort();                                                           \
  } while (0)

/**
 * @def ANU_DIE
 * @brief Print message and exit as we have encountered external error.
 * @note Used when we encounter issues such as memory allocation failure.
 */
#define ANU_DIE(message)                                               \
  do {                                                                 \
    (void) fprintf(stderr, "[FATAL]: %s:%d: %s\n", __FILE__, __LINE__, \
                   (message));                                         \
    (void) fflush(stderr);                                             \
    abort();                                                           \
  } while (0)

/**
 * @def TODO
 * @brief Print message and exit, as this section of code is not implemented yet.
 */
#define TODO(message)                                               \
  do {                                                              \
    (void) fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, \
                   (message));                                      \
    (void) fflush(stderr);                                          \
    abort();                                                        \
  } while (0)

#ifdef ANU_DEBUG  // If its in DEBUG MODE

/* Debug builds should crash when reaching unreachable code. */
#  define UNREACHABLE(message)                                          \
    do {                                                                \
      (void) fprintf(stderr,                                            \
                     "[PANIC] UNREACHABLE CODE REACHED AT %s:%d: %s\n", \
                     __FILE__, __LINE__, (message));                    \
      abort();                                                          \
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
      if (!(cond)) {   \
        UNREACHABLE(); \
      }                \
    } while (0)

#endif  // UNREACHABLE

#endif  // ANU_UTIL_H
