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
typedef uint32_t bflag32;

/**
 * @name BitMacros Flag Macros
 * Macros for safe bitflag manipulation.
 * @{
 */

/**
 * @def ANU_SET_FLAG
 * @brief Sets one or more flags in a bitmask.
 *
 * ```c
 * ANU_SET_FLAG(player_state, STATUS_RUNNING | STATUS_POISONED);
 * ```
 */
#define ANU_SET_FLAG(mask, flag) ((mask) |= (flag))

/** Clears one or more flags from a bitmask. */
#define ANU_CLEAR_FLAG(mask, flag) ((mask) &= ~(flag))

/**
 * @def ANU_TOGGLE_FLAG
 * @brief Toggles one or more flags in a bitmask.
 */
#define ANU_TOGGLE_FLAG(mask, flag) ((mask) ^= (flag))

/**
 * @def ANU_HAS_ALL_FLAGS
 * @brief Checks if ALL specified flags are set.
 * @note If flag is 0, this will return true.
 * ```c
 * if (ANU_HAS_ALL_FLAGS(player_state, STATUS_RUNNING | STATUS_POISONED)) {
 *  printf("Player is losing health fast!\n");
 * }
 * ```
 */
#define ANU_HAS_ALL_FLAGS(mask, flag) (((mask) & (flag)) == (flag))

/**
 * @def ANU_HAS_ANY_FLAG
 * @brief Checks if ANY of the specified flags are set.
 */
#define ANU_HAS_ANY_FLAG(mask, flag) (((mask) & (flag)) != 0)

/** @} */  // End BitMacros group

/**
 * @def CACHE_LINE_SIZE
 * @brief Number of bytes in a single cache line.
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
/** Number of integer types that fit within a single cache line. */
#define CACHE_STRIDE_INT (CACHE_LINE_SIZE / sizeof(int))
/** Number of `long` types that fit within a single cache line. */
#define CACHE_STRIDE_LONG (CACHE_LINE_SIZE / sizeof(long))
/** Number of `long long` types that fit within a single cache line. */
#define CACHE_STRIDE_LLONG (CACHE_LINE_SIZE / sizeof(long long))
/** Number of `char` types that will fit within a single cache line. */
#define CACHE_STRIDE_CHAR (CACHE_LINE_SIZE / sizeof(char))
/** Number of `float` types that will fit within a single cache line. */
#define CACHE_STRIDE_FLOAT (CACHE_LINE_SIZE / sizeof(float))
/** Number of `double` types that will fit within a single cache line. */
#define CACHE_STRIDE_DOUBLE (CACHE_LINE_SIZE / sizeof(double))

/**
 * @name Time conversion utilities
 * Useful constants and inline functions to convert between different time bases.
 * @{
 */

/**
 * @brief One second (s) in microseconds (us).
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
 * Macros to help with numbers and math.
 * @{
 */

/** Return larger value from X and Y. */
#define MAXIMUM(X, Y) ((X) > (Y) ? (X) : (Y))

/** Return smallest number between X and Y */
#define MINIMUM(X, Y) ((X) < (Y) ? (X) : (Y))

/** Absolute value of X */
#define ABSOLUTE(X) ((X) > 0 ? (X) : -(X))

/** Difference of X and Y. */
#define DIFF(X, Y) ((X) > (Y) ? (X) - (Y) : (Y) - (X))

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
 * Macros to convert sizes to their equivalent value in bytes.
 * E.g. KILOBYTE(10) == 10,000 bytes.
 * @{
 */
#define KILOBYTE(X) ((X) * 1000ULL)          ///< KB to Bytes (SI)
#define MEGABYTE(X) (KILOBYTE(X) * 1000ULL)  ///< MB to Bytes (SI)
#define GIGABYTE(X) (MEGABYTE(X) * 1000ULL)  ///< GB to Bytes (SI)
#define TERABYTE(X) (GIGABYTE(X) * 1000ULL)  ///< TB to Bytes (SI)

#define KIBIBYTE(X) ((X) * 1024ULL)          ///< KiB to Bytes (IEC)
#define MEBIBYTE(X) (KIBIBYTE(X) * 1024ULL)  ///< MiB to Bytes (IEC)
#define GIBIBYTE(X) (MEBIBYTE(X) * 1024ULL)  ///< GiB to Bytes (IEC)
#define TEBIBYTE(X) (GIBIBYTE(X) * 1024ULL)  ///< TiB to Bytes (IEC)
/** @} */

static_assert(
    sizeof(unsigned long long) >= 8,
    "Unsigned long longs must be at least 64 bits for our hamming distance "
    "implementation to work.");

/**
 * @brief Calculate hamming distance between two **unsigned** 64-bit integers.
 * Makes use of `__builtin_popcountll()`.
 * @return Number of bits that differ between `X` and `Y` as an integer.
 * @retval 0 `X` and `Y` are the exact same.
 * @retval 64 `X` and `Y` are compliments of one another.
 * @retval k `X` and `Y` differ by `k` number of bits.
 */
static inline unsigned int hamming_distance (const uint64_t a,
                                             const uint64_t b) {
  return (unsigned) __builtin_popcountll(a ^ b);
}

void print_matrix_float(FILE *fp, const float *matrix, int rows, int cols);

void anu_util_print_indent(FILE *fp, int spaces, int depth);

static inline int anu_util_tolower (int c) {
  return 'A' <= c && c <= 'Z' ? c + ('a' - 'A') : c;
}

/** @def ANU_ARRAY_SIZE
 * @brief Calculate the length of a C array
 *
 * @note This should be called with a real array.
 * @warning Calling this with a pointer is an error.
 * A mechanism to detect many (though not all) of those errors at compile
 * time is implemented. It works by the second division producing a division by
 * zero in those cases (-Wdiv-by-zero in GCC).
 *
 * Snippet derived from neovim (neovim/src/nvim/macros_defs.h)
 * Licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0/
 * Renamed to `ANU_ARRAY_SIZE`.
 */
#define ANU_ARRAY_SIZE(array)             \
  ((sizeof(array) / sizeof((array)[0])) / \
   ((size_t) (!(sizeof(array) % sizeof((array)[0])))))

/**
 * @def ARRAY_LAST_ENTRY
 * @brief Get last array entry
 *
 * @note This should be called with a real array.
 * @warning Calling this with a pointer is an *error*.
 *
 * Snippet derived from neovim (neovim/src/nvim/macros_defs.h)
 * Licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
#define ARRAY_LAST_ENTRY(array) (array)[ANU_ARRAY_SIZE(array) - 1]

/** Zero out memory. */
#define ZERO_MEMORY(pointer, count, type) \
  memset((pointer), 0, (count) * sizeof(type))

#if defined(__GNUC__) || defined(__clang__)

#  define MAYBE_UNUSED __attribute__((unused))
#  define ALWAYS_INLINE inline __attribute__((always_inline))
#  define FLATTEN __attribute__((flatten))
#  define HOT_FUNC __attribute__((hot))
#  define COLD_FUNC __attribute__((cold))

/** @def LIKELY
 * Hint to compiler that the branch is most likely *TRUE*.
 */
#  define LIKELY(x) __builtin_expect(!!(x), 1)

/** @def UNLIKELY
 * Hint to the compiler the condition is most likely *FALSE*.
 */
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)

#else

#  define MAYBE_UNUSED
#  define ALWAYS_INLINE
#  define FLATTEN
#  define HOT_FUNC
#  define COLD_FUNC
#  define LIKELY(x) (x)
#  define UNLIKELY(x) (x)
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
