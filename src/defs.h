#ifndef ANU_DEFS_H_
#define ANU_DEFS_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @name Type Definitions
 * Shorthand fixed-width integer and primitive type definitions used throughout the codebase.
 * @{
 */
/* clang-format off */
typedef uint8_t   u8;       /**< 8-bit unsigned integer */
typedef int32_t   i32;      /**< 32-bit signed integer */
typedef int64_t   i64;      /**< 64-bit signed integer */
typedef uint32_t  u32;      /**< 32-bit unsigned integer */
typedef uint64_t  u64;      /**< 64-bit unsigned integer */
typedef float     f32;      /**< 32-bit floating-point number (single precision) */
typedef double    f64;      /**< 64-bit floating-point number (double precision) */

typedef uint8_t   byte;     /**< Raw byte representation (alias for u8/uint8_t) */
typedef uintptr_t uptr;     /**< Unsigned integer capable of holding a pointer securely */
typedef ptrdiff_t size;     /**< Signed integer for pointer arithmetic or negative sizes */
typedef size_t    usize;    /**< Unsigned integer for object sizes, memory sizing, and array indexing */
typedef uint32_t  flags32;  /**< 32-bit unsigned integer explicitly used for bitwise flags/masks */
/* clang-format on */

/** @} */  // END Type Definitions

typedef struct hash_entry {
  u64 hash;
  u64 timestamp;
} hash_entry;

typedef enum ANU_STATUS : int32_t {
  ANU_OK = 0,                 /**< Function executed successfully. */
  ANU_IO_FAIL,                /**< IO failure. */
  ANU_OOM,                    /**< Out of memory/ could not allocate memory. */
  ANU_STATUS_FILE_PENDING,    /**< File is pending processing. */
  ANU_STATUS_FILE_CACHED,     /**< File is already in the database. */
  ANU_SKIPPED_SHORT_DURATION, /**< Video file is too short to process. */
  ANU_FRAME_BLACK,            /**< Frame is too dark to process. */
  ANU_LIBAV_FAIL,             /**< Some error occured whilst using libav */
} ANU_STATUS;

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

#endif  // ANU_DEFS_H_
