/**
 * @file kvec.h
 * @brief A simple, portable, and macro-based dynamic array (vector) library for C.
 *
 * Based on klib's kvec by Attractive Chaos, adapted for portability.
 *
 * The library provides two types of vectors:
 * 1. Standard vector (`kvec_t`): Purely heap-allocated dynamic array.
 * 2. Inline vector (`kvec_withinit_t`): Begins with a stack-allocated array
 *    and seamlessly upgrades to a heap allocation if it exceeds its initial capacity.
 *
 * @par Example Usage:
 * @code
 *     #include "kvec.h"
 *     int main() {
 *       kvec_t(int) array = KV_INITIAL_VALUE;
 *       kv_push(array, 10);  // append
 *       kv_a(array, 20) = 5; // dynamic access (auto-resizes to fit index 20)
 *       kv_A(array, 20) = 4; // static access (overwrites index 20)
 *       kv_destroy(array);
 *       return 0;
 *     }
 * @endcode
 */

// The MIT License
//
// Copyright (c) 2008, by Attractive Chaos <attractor@live.co.uk>
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PORTABLE_KVEC_H
#define PORTABLE_KVEC_H

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 * @defgroup kvec_config Configuration & Custom Allocators
 * Define these macros before including this header to use custom memory allocators.
 * @{
 */
#ifndef KVEC_MALLOC
#  define KVEC_MALLOC malloc
#endif

#ifndef KVEC_REALLOC
#  define KVEC_REALLOC realloc
#endif

#ifndef KVEC_FREE
#  define KVEC_FREE free
#endif

#ifndef KVEC_FREE_CLEAR
#  define KVEC_FREE_CLEAR(ptr)   \
    do {                         \
  KVEC_FREE((void *)(ptr)); \
      (ptr) = NULL;              \
    } while (0)
#endif
/** @} */

#ifndef KV_ARRAY_SIZE
#  define KV_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

// Portable 'restrict' keyword handling
#ifndef KVEC_RESTRICT
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    define KVEC_RESTRICT restrict
#  elif defined(__cplusplus)
#    define KVEC_RESTRICT
#  elif defined(__GNUC__) || defined(__clang__)
#    define KVEC_RESTRICT __restrict__
#  elif defined(_MSC_VER)
#    define KVEC_RESTRICT __restrict
#  else
#    define KVEC_RESTRICT
#  endif
#endif

/**
 * @brief Helper to handle realloc safely.
 * If realloc fails, we abort to prevent a memory leak and undefined behavior.
 * This satisfies clang-tidy's [bugprone-suspicious-realloc-usage].
 */
static inline void *kv_realloc_safe (void *ptr, const size_t new_sz) {
  void *new_ptr = KVEC_REALLOC(ptr, new_sz);
  if (new_sz > 0 && !new_ptr) {
    // If you have a custom DIE macro, you can use it here.
    // Otherwise, abort() is the safest way to handle OOM in a library.
    abort();
  }
  return new_ptr;
}

/**
 * @brief Helper: Move data to a new destination and free source.
 * @internal
 */
static inline void *_memcpy_free (void *KVEC_RESTRICT dest,
                                  void *KVEC_RESTRICT src,
                                  const size_t sz) {
  memcpy(dest, src, sz);
  KVEC_FREE_CLEAR(src);
  return dest;
}



/**
 * @brief Rounds an integer up to the next highest power of 2.
 * @param x The variable to round up.
 */
#define kv_roundup32(x)                                                 \
  ((--(x)),                                                             \
   ((x) |= (x) >> 1, (x) |= (x) >> 2, (x) |= (x) >> 4, (x) |= (x) >> 8, \
    (x) |= (x) >> 16),                                                  \
   (++(x)))

/* ========================================================================= */
/* STANDARD VECTOR (HEAP ALLOCATED)                                          */
/* ========================================================================= */

/**
 * @brief Static initializer for a standard vector.
 */
#define KV_INITIAL_VALUE {.size = 0, .capacity = 0, .items = NULL}

/**
 * @brief Defines a vector structure for a specific type.
 * @param type The C data type the vector will hold.
 */
// NOLINTBEGIN (bugprone-macro-parentheses)
#define kvec_t(type) \
  struct {           \
    size_t size;     \
    size_t capacity; \
    type *items;     \
  }
// NOLINTEND

/**
 * @brief Initializes a standard vector.
 * @param[out] v The vector to initialize.
 */
#define kv_init(v) ((v).size = (v).capacity = 0, (v).items = 0)

/**
 * @brief Destroys a standard vector, freeing its memory.
 * @param[in,out] v The vector to destroy.
 */
#define kv_destroy(v)              \
  do {                             \
    KVEC_FREE((void *) (v).items); \
    kv_init(v);                    \
  } while (0)

/**
 * @brief Static element access (no bounds checking).
 * @param v The vector.
 * @param i Index of the element.
 * @return L-value reference to the element at index `i`.
 */
#define kv_A(v, i) ((v).items[(i)])

/**
 * @brief Pops the last element from the vector and returns it.
 * @param[in,out] v The vector.
 * @return The popped element.
 */
#define kv_pop(v) ((v).items[--(v).size])

/**
 * @brief Gets the number of elements currently in the vector.
 * @param v The vector.
 * @return The size of the vector.
 */
#define kv_size(v) ((v).size)

/**
 * @brief Gets the current maximum capacity of the vector.
 * @param v The vector.
 * @return The capacity.
 */
#define kv_max(v) ((v).capacity)

/**
 * @brief Accesses an element starting from the end of the vector.
 * @param v The vector.
 * @param i Index relative to the end (0 is the last element).
 * @return L-value reference to the element.
 */
#define kv_Z(v, i) kv_A(v, kv_size(v) - (i) - 1)

/**
 * @brief Gets the last element in the vector.
 * @param v The vector.
 * @return L-value reference to the last element.
 */
#define kv_last(v) kv_Z(v, 0)

/**
 * @brief Drop last n items from kvec without resizing.
 * @param[in,out] v Kvec to drop items from.
 * @param[in]     n Number of elements to drop.
 */
#define kv_drop(v, n) ((v).size -= (n))

/**
 * @brief Resizes the capacity of the vector to exactly `s`.
 * @param[in,out] v The vector.
 * @param[in]     s New capacity size.
 */
#define kv_resize(v, s)                                 \
  ((v).capacity = (s),                                  \
   (v).items = (__typeof__((v).items)) kv_realloc_safe( \
       (void *) (v).items, sizeof((v).items[0]) * (v).capacity))

/**
 * @brief Doubles the capacity of the vector (or sets it to 8 if currently 0).
 * @param[in,out] v The vector.
 */
#define kv_resize_full(v) kv_resize(v, (v).capacity ? (v).capacity << 1 : 8)

/**
 * @brief Copies the contents of one standard vector to another.
 * @param[out] v1 Destination vector.
 * @param[in]  v0 Source vector.
 */
#define kv_copy(v1, v0)                                                \
  do {                                                                 \
    if ((v1).capacity < (v0).size) {                                   \
      kv_resize(v1, (v0).size);                                        \
    }                                                                  \
    (v1).size = (v0).size;                                             \
  memcpy((void *)(v1).items, (const void *)(v0).items, sizeof((v1).items[0]) * (v0).size); \
  } while (0)

/**
 * @brief Ensures the vector has enough capacity for at least `len` more items.
 * @param[in,out] v   The vector.
 * @param[in]     len Number of additional elements to make space for.
 */
#define kv_ensure_space(v, len)            \
  do {                                     \
    if ((v).capacity < (v).size + (len)) { \
      (v).capacity = (v).size + (len);     \
      kv_roundup32((v).capacity);          \
      kv_resize((v), (v).capacity);        \
    }                                      \
  } while (0)

/**
 * @brief Appends an array of raw data to the vector.
 * @param[in,out] v    The vector.
 * @param[in]     data Pointer to the data to append.
 * @param[in]     len  Number of elements to append.
 */
#define kv_concat_len(v, data, len)                                   \
  if ((len) > 0) {                                                    \
    kv_ensure_space(v, len);                                          \
    assert((v).items);                                                \
  memcpy((void *)((v).items + (v).size), (const void *) (data), sizeof((v).items[0]) * (len)); \
    (v).size = (v).size + (len);                                      \
  }

/**
 * @brief Appends a null-terminated string to a character vector.
 * @param[in,out] v   The vector.
 * @param[in]     str Null-terminated string to append.
 */
#define kv_concat(v, str) kv_concat_len(v, str, strlen(str))

/**
 * @brief Splices (appends) the contents of one vector into another.
 * @param[in,out] v1 Destination vector.
 * @param[in]     v0 Source vector.
 */
#define kv_splice(v1, v0) kv_concat_len(v1, (v0).items, (v0).size)

/**
 * @brief Gets a pointer to the next free slot, expanding capacity if needed.
 * @param[in,out] v The vector.
 * @return Pointer to the newly allocated uninitialized slot.
 */
#define kv_pushp(v)                                           \
  ((((v).size == (v).capacity) ? (kv_resize_full(v), 0) : 0), \
   ((v).items + ((v).size++)))

/**
 * @brief Pushes an element to the end of the vector, expanding if needed.
 * @param[in,out] v The vector.
 * @param[in]     x The value to push.
 */
#define kv_push(v, x) (*kv_pushp(v) = (x))

/**
 * @brief Fast get pointer to next slot WITHOUT checking capacity.
 * @warning User MUST ensure `v.capacity > v.size` before calling.
 */
#define kv_pushp_c(v) ((v).items + ((v).size++))

/**
 * @brief Fast push WITHOUT checking capacity.
 * @warning User MUST ensure `v.capacity > v.size` before calling.
 */
#define kv_push_c(v, x) (*kv_pushp_c(v) = (x))

/**
 * @brief Dynamic element access. Expands capacity and size if index is out of bounds.
 * @param[in,out] v The vector.
 * @param[in]     i The index to access.
 * @return L-value reference to the element at index `i`.
 */
#define kv_a(v, i)                                                          \
  (*(((v).capacity <= (size_t) (i)                                          \
          ? ((v).capacity = (v).size = (i) + 1, kv_roundup32((v).capacity), \
             kv_resize((v), (v).capacity), 0UL)                             \
          : ((v).size <= (size_t) (i) ? (v).size = (i) + 1 : 0UL)),         \
     &(v).items[(i)]))

/**
 * @brief Removes `n` elements starting at index `i` by shifting elements left.
 * @param[in,out] v The vector.
 * @param[in]     i Index to start removing from.
 * @param[in]     n Number of elements to remove.
 */
#define kv_shift(v, i, n)                                        \
  ((v).size -= (n),                                              \
  (i) < (v).size && memmove((void *)&kv_A(v, (i)), (const void *)&kv_A(v, (i) + (n)), \
                             ((v).size - (i)) * sizeof(kv_A(v, i))))

/* ========================================================================= */
/* INLINE VECTOR (INITIALLY STACK ALLOCATED)                                 */
/* ========================================================================= */

/**
 * @brief Type of a vector with a few first members allocated on stack.
 *
 * If it outgrows `INIT_SIZE`, it will transition to a heap allocation automatically.
 * Compatible with `#kv_A`, `#kv_pop`, `#kv_size`, `#kv_max`, `#kv_last`.
 * @warning NOT compatible with standard `#kv_resize`, `#kv_push`, `#kv_destroy`, etc.
 *          Use the `kvi_*` macro equivalents for operations that change capacity.
 *
 * @param type      Type of vector elements.
 * @param INIT_SIZE Number of the elements in the initial array.
 */
// NOLINTBEGIN (bugprone-macro-parentheses)
#define kvec_withinit_t(type, INIT_SIZE) \
  struct {                               \
    size_t size;                         \
    size_t capacity;                     \
    type *items;                         \
    type init_array[INIT_SIZE];          \
  }
// NOLINTEND

/**
 * @brief Static initializer for an inline vector.
 * @param v The uninitialized vector struct.
 */
#define KVI_INITIAL_VALUE(v)                  \
  {.size = 0,                                 \
   .capacity = KV_ARRAY_SIZE((v).init_array), \
   .items = (v).init_array}

/**
 * @brief Initialize vector with its preallocated array.
 * @param[out] v Vector to initialize.
 */
#define kvi_init(v)                                            \
  ((v).capacity = KV_ARRAY_SIZE((v).init_array), (v).size = 0, \
   (v).items = (v).init_array)


/**
 * @brief Resizes an inline vector.
 *
 * Handles the transition between stack (`init_array`) and heap.
 * @note May not resize to an array smaller than `init_array`.
 *
 * @param[in,out] v Vector to resize.
 * @param[in]     s New size/capacity.
 */
#define kvi_resize(v, s)                                                      \
  ((v).capacity =                                                             \
       ((s) > KV_ARRAY_SIZE((v).init_array) ? (s)                             \
                                            : KV_ARRAY_SIZE((v).init_array)), \
   (v).items = (__typeof__((v).items))                                        \
       ((v).capacity == KV_ARRAY_SIZE((v).init_array)                         \
            ? ((v).items == (v).init_array                                    \
                   ? (v).items                                                \
  : (__typeof__((v).items)) _memcpy_free((void*) (v).init_array, \
  (void *)(v).items, (v).size * sizeof((v).items[0])))           \
  : (__typeof__((v).items)) ((v).items == (v).init_array                                    \
  ? (__typeof__((v).items)) memcpy(KVEC_MALLOC((v).capacity * sizeof((v).items[0])), \
  (const void *) (v).items, (v).size * sizeof((v).items[0]))       \
  :(__typeof__((v).items)) KVEC_REALLOC((void *)(v).items,                                  \
                                  (v).capacity * sizeof((v).items[0])))))

/**
 * @brief Doubles the capacity of an inline vector when it is full.
 * @param[in,out] v Vector to resize.
 */
#define kvi_resize_full(v)                                                    \
  /* KV_ARRAY_SIZE((v).init_array) is the minimal capacity of this vector. */ \
  /* Thus when vector is full capacity may not be zero and it is safe */      \
  /* not to bother with checking whether (v).capacity is 0. But now */        \
  /* capacity is not guaranteed to have size that is a power of 2, it is */   \
  /* hard to fix this here and is not very necessary if users will use */     \
  /* 2^x initial array size. */                                               \
  kvi_resize(v, (v).capacity << 1)

/**
 * @brief Ensures inline vector has capacity for at least `len` more items.
 * @param[in,out] v   The inline vector.
 * @param[in]     len Number of additional elements to guarantee space for.
 */
#define kvi_ensure_more_space(v, len)      \
  do {                                     \
    if ((v).capacity < (v).size + (len)) { \
      (v).capacity = (v).size + (len);     \
      kv_roundup32((v).capacity);          \
      kvi_resize((v), (v).capacity);       \
    }                                      \
  } while (0)

/**
 * @brief Appends an array of raw data to the inline vector.
 * @param[in,out] v    The inline vector.
 * @param[in]     data Pointer to the data.
 * @param[in]     len  Number of items.
 */
#define kvi_concat_len(v, data, len)                                  \
  if ((len) > 0) {                                                    \
    kvi_ensure_more_space(v, len);                                    \
    assert((v).items);                                                \
  memcpy((void *)(v).items + (v).size, (void *)(data), sizeof((v).items[0]) * (len)); \
    (v).size = (v).size + (len);                                      \
  }

/**
 * @brief Appends a null-terminated string to a character inline vector.
 * @param[in,out] v   The inline vector.
 * @param[in]     str Null-terminated string.
 */
#define kvi_concat(v, str) kvi_concat_len(v, str, strlen(str))

/**
 * @brief Splices one inline vector onto the end of another.
 * @param[in,out] v1 Destination inline vector.
 * @param[in]     v0 Source inline vector.
 */
#define kvi_splice(v1, v0) kvi_concat_len(v1, (v0).items, (v0).size)

/**
 * @brief Get location where to store new element to an inline vector.
 * Expands capacity if necessary.
 * @param[in,out] v Vector to push to.
 * @return Pointer to the place where the new value should be stored.
 */
#define kvi_pushp(v)                                           \
  ((((v).size == (v).capacity) ? (kvi_resize_full(v), 0) : 0), \
   ((v).items + ((v).size++)))

/**
 * @brief Push value to a vector with a preallocated inline array.
 * @param[in,out] v Vector to push to.
 * @param[in]     x Value to push.
 */
#define kvi_push(v, x) (*kvi_pushp(v) = (x))

/**
 * @brief Copy a vector to a preallocated inline vector.
 * @param[out] v1 destination (must be inline vector)
 * @param[in]  v0 source (can be either standard vector or inline vector)
 */
#define kvi_copy(v1, v0)                                               \
  do {                                                                 \
    if ((v1).capacity < (v0).size) {                                   \
      kvi_resize(v1, (v0).size);                                       \
    }                                                                  \
    (v1).size = (v0).size;                                             \
  memcpy((void *)(v1).items, (const void *)(v0).items, sizeof((v1).items[0]) * (v0).size); \
  } while (0)

/**
 * @brief Frees an inline vector's memory ONLY if it transitioned to the heap.
 * @note This must be used instead of `kv_destroy` to prevent freeing stack memory.
 * @param[in,out] v Vector to free.
 */
#define kvi_destroy(v)                 \
  do {                                 \
    if ((v).items != (v).init_array) { \
      KVEC_FREE_CLEAR((v).items);      \
    }                                  \
  } while (0)

#endif  // PORTABLE_KVEC_H
