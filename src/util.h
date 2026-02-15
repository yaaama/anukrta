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

/* One second in microseconds */
#define ANU_TIME_ONE_SEC_IN_US 1000000
/* Convert microseconds to seconds */
#define ANU_US_TO_SECONDS(A) ((double)(A) / (ANU_TIME_ONE_SEC_IN_US))

/* Array size macro */
#define ANU_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* Return bigger value */
#define MAXIMUM(x, y) ((x) > (y) ? (x) : (y))
/* Return smaller value */
#define MINIMUM(x, y) ((x) < (y) ? (x) : (y))
/* Range constraint macro to ensure value is between min and max */
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM(_val, _max), _min)

u32 hamming_distance(uint64_t hash1, uint64_t hash2);
int compare_floats(const void *a, const void *b);
void debug_print_matrix(const float *matrix, int rows, int cols);
void anu_util_print_indent(int depth);
int anu_util_tolower(int c);

#endif  // ANU_UTIL_H
