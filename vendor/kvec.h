/**
 * @file kvec.h
 * @brief A macro-based dynamic array (vector) library for C.
 *
 * Based on Neovim's kvec.h, which was based on kvec by Attractive Chaos.
 * This version requires GCC or Clang (GNU statement expressions and typeof) and adds:
 *
 *  - Compile-time element type checks for kv_push(), kv_push_all(),
 *    kv_concat_len(), kv_splice(), kv_copy(), kv_swap(), ...
 *  - We've tried to reduce macro argument evaluation.
 *  - Optional bounds assertions on element access (`kvec_bounds_()`).
 *  - Overflow-checked size arithmetic: allocation-size overflow aborts
 *    instead of silently corrupting the heap.
 *
 * The library provides two types of vectors:
 * 1. Standard vector (`kvec_t`): purely heap-allocated dynamic array.
 * 2. Inline vector (`kvec_withinit_t`): begins with a stack-allocated array
 *    and seamlessly upgrades to a heap allocation if it outgrows it.
 *
 * @par Example Usage:
 * @code
 *     #include "kvec.h"
 *     int main(void) {
 *       KVEC_TYPEDEF(int, int_vec);
 *       int_vec array = KV_INITIAL_VALUE;
 *       kv_push(array, 10);         // append (type checked)
 *       kv_push_all(array, 1, 2, 3);
 *       kv_a(array, 20) = 5;        // dynamic access (auto-grows)
 *       kv_A(array, 20) = 4;        // static access (bounds asserted)
 *       kv_foreach (array, it) printf("%d\n", *it);
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The implementation needs GNU statement expressions and typeof. */
#if !defined(__GNUC__) && !defined(__clang__)
#  error "kvec.h requires GCC or Clang."
#endif

/**
 * @defgroup kvec_config Configuration & Custom Allocators
 * Define these macros before including this header to use custom memory allocators.
 * @{
 */
#if !defined(KVEC_MALLOC) || !defined(KVEC_REALLOC)
#  include "mem.h"
#  define KVEC_MALLOC xmalloc
#  define KVEC_REALLOC xrealloc
#  define KVEC_FREE free
#endif

/** @} */

#define KVEC_CAT_(a, b) a##b
#define KVEC_CAT(a, b) KVEC_CAT_(a, b)
#define KVEC_UNIQ(base) KVEC_CAT(base, __COUNTER__)

#define KVEC_FREE_CLEAR(ptr)   \
  do {                         \
    KVEC_FREE((void *) (ptr)); \
    (ptr) = NULL;              \
  } while (0)

#ifndef KV_ARRAY_SIZE
#  define KV_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#  define kvec_typeof typeof
#else
#  define kvec_typeof __typeof__
#endif

#define KVEC_ASSERT(x) assert(x)
#define kvec_bound_(cond) KVEC_ASSERT(cond)

/** @internal Compile-time check that two expressions have compatible types. */
#define KVEC_ASSERT_SAME_TYPE(a, b, msg) \
  _Static_assert(__builtin_types_compatible_p(kvec_typeof(a), kvec_typeof(b)), msg)

/** @internal Byte size of @p n elements of @p v, aborting on overflow. */
#define kvec_bytes_(v, n) kv_checked_mul((n), sizeof((v).items[0]))

/** @brief Aborts instead of returning an undersized allocation size. */
static inline size_t kv_checked_mul (size_t a, size_t b) {
  size_t result = 0;
  if (__builtin_mul_overflow(a, b, &result) || b == 0) {
    __builtin_trap();
  }
  return result;
}

/** @brief Aborts on addition overflow. */
static inline size_t kv_checked_add (size_t a, size_t b) {
  size_t result = 0;
  if (__builtin_add_overflow(a, b, &result)) {
    __builtin_trap();
  }
  return result;
}

/** @brief malloc() wrapper that aborts on failure. */
static inline void *kv_malloc_safe (size_t n) {
  void *p = KVEC_MALLOC(n);
  if (n > 0 && !p) {
    __builtin_trap();
  }
  return p;
}

/**
 * @brief Helper to handle realloc safely.
 * If realloc fails, we abort to prevent a memory leak and undefined behavior.
 * This satisfies clang-tidy's [bugprone-suspicious-realloc-usage].
 */
static inline void *kv_realloc_safe (void *ptr, const size_t new_sz) {
  void *new_ptr = KVEC_REALLOC(ptr, new_sz);
  if (new_sz > 0 && !new_ptr) {
    __builtin_trap();
  }
  return new_ptr;
}

/**
 * @brief Rounds up to the next power of two, aborting on impossible sizes.
 * This is 64-bit safe and evaluates its argument once.
 */
static inline size_t kv_roundup_size (size_t n) {
  if (n > SIZE_MAX / 2) {
    __builtin_trap();
  }
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFFu
  n |= n >> 32;
#endif
  return n + 1;
}

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
 * @brief Creates a named vector type, e.g. `KVEC_TYPEDEF(int, int_vec);`.
 *
 * Declaring all vectors of an element type through one typedef guarantees
 * they share a single compatible struct type.
 */
#define KVEC_TYPEDEF(type, name) typedef kvec_t(type) name

/**
 * @brief Initializes a standard vector.
 * @param[out] v The vector to initialize.
 */
#define kv_init(v) ((v).size = (v).capacity = 0, (v).items = 0)

/**
 * @brief Destroys a standard vector, freeing its memory.
 * The vector is left empty and may be reused afterwards.
 * @warning Must NOT be used on kvec_withinit_t vectors (use kvi_destroy()).
 */
#define kv_destroy(v)              \
  do {                             \
    KVEC_FREE((void *) (v).items); \
    kv_init(v);                    \
  } while (0)

/**
 * @brief Static element access (no bounds checking).
 * @return L-value reference to the element at index `i`.
 */
#define kv_A(v, i)                       \
  (*({                                   \
    const size_t kvec_i_ = (size_t) (i); \
    kvec_bound_(kvec_i_ < (v).size);     \
    &(v).items[kvec_i_];                 \
  }))

/**
 * @brief Accesses an element starting from the end of the vector.
 * @param i Index relative to the end (0 is the last element).
 * @return L-value reference to the element.
 */
#define kv_Z(v, i)                        \
  (*({                                    \
    const size_t kvec_i_ = (size_t) (i);  \
    kvec_bound_(kvec_i_ < (v).size);      \
    &((v).items[(v).size - kvec_i_ - 1]); \
  }))

/** @brief Pops the last element from the vector and returns it. */
#define kv_pop(v)              \
  ({                           \
    kvec_bound_((v).size > 0); \
    (v).items[--(v).size];     \
  })

/** @brief Gets the number of elements currently in the vector. */
#define kv_size(v) ((v).size)

/** @brief Gets the current maximum capacity of the vector. */
#define kv_max(v) ((v).capacity)

/** @brief Gets the last element in the vector (l-value). */
#define kv_last(v) kv_Z(v, 0)

/** @brief First element in the vector (l-value). */
#define kv_first(v) kv_A(v, 0)

/** @brief True if the vector holds no elements. */
#define kv_empty(v) ((v).size == 0)

/** @brief Resets the size to zero without releasing memory. */
#define kv_clear(v) ((v).size = 0)

/**
 * @brief Drop last n items from kvec without resizing.
 * @param[in] n Number of elements to drop.
 */
#define kv_drop(v, n)                    \
  ({                                     \
    const size_t kvec_n_ = (size_t) (n); \
    kvec_bound_(kvec_n_ <= (v).size);    \
    (v).size -= kvec_n_;                 \
  })

/**
 * @brief Resizes the capacity of the vector to exactly `s`.
 * Aborts if `s` < current size. A resize to 0 releases the buffer.
 */
#define kv_resize(v, s)                                                                                  \
  ({                                                                                                     \
    const size_t kvec_new_cap_ = (size_t) (s);                                                           \
    kvec_bound_((v).size <= kvec_new_cap_);                                                              \
    if (kvec_new_cap_ == 0) {                                                                            \
      KVEC_FREE((void *) (v).items);                                                                     \
      (v).items = NULL;                                                                                  \
    } else {                                                                                             \
      (v).items =                                                                                        \
          (kvec_typeof((v).items)) kv_realloc_safe((void *) (v).items, kvec_bytes_((v), kvec_new_cap_)); \
    }                                                                                                    \
    (v).capacity = kvec_new_cap_;                                                                        \
    (v).items;                                                                                           \
  })

/** @brief Doubles the capacity of the vector (or sets it to 8 if currently 0). */
#define kv_resize_full(v) kv_resize((v), (v).capacity ? kv_checked_add((v).capacity, (v).capacity) : 8)

/**
 * @brief Copies the contents of vector `v0` into `v1`.
 * Compile-time error if the element types differ. `kv_copy(v, v)` is a no-op.
 */
#define kv_copy(v1, v0)                                                                   \
  ({                                                                                      \
    KVEC_ASSERT_SAME_TYPE((v1).items[0], (v0).items[0], "kv_copy: element types differ"); \
    const kvec_typeof((v1).items[0]) *kvec_src_ = (v0).items;                             \
    const size_t kvec_n_ = (v0).size;                                                     \
    if (kvec_n_ > 0 && &(v1).size != &(v0).size) {                                        \
      if ((v1).capacity < kvec_n_) {                                                      \
        kv_resize(v1, kvec_n_);                                                           \
      }                                                                                   \
      memcpy((void *) (v1).items, (const void *) kvec_src_, kvec_bytes_((v1), kvec_n_));  \
    }                                                                                     \
    (v1).size = kvec_n_;                                                                  \
    (v1).size;                                                                            \
  })

/**
 * @brief Exchanges the contents of two vectors (O(1), pointers only).
 * Both operands must be the exact same vector type (use a shared
 * KVEC_TYPEDEF); this also prevents accidentally swapping a standard vector
 * with an inline one, which would dangle its `items` pointer.
 */
#define kv_swap(a, b)                                                       \
  ({                                                                        \
    KVEC_ASSERT_SAME_TYPE(a, b,                                             \
                          "kv_swap: operands must be the same vector type " \
                          "(declare them through the same KVEC_TYPEDEF)");  \
    kvec_typeof(a) kvec_tmp_ = (a);                                         \
    (a) = (b);                                                              \
    (b) = kvec_tmp_;                                                        \
  })

/** @brief Ensures capacity for at least @p `len` *more* items. */
#define kv_ensure_space(v, len)                                         \
  ({                                                                    \
    const size_t kvec_need_ = kv_checked_add((v).size, (size_t) (len)); \
    if ((v).capacity < kvec_need_) {                                    \
      kv_resize(v, kv_roundup_size(kvec_need_));                        \
    }                                                                   \
    (v).capacity;                                                       \
  })

/** @brief Ensures a total capacity of at least `n` elements. */
#define kv_reserve(v, n)                         \
  ({                                             \
    const size_t kvec_need_ = (size_t) (n);      \
    if ((v).capacity < kvec_need_) {             \
      kv_resize(v, kv_roundup_size(kvec_need_)); \
    }                                            \
    (v).capacity;                                \
  })

/**
 * @brief Appends `len` elements from `data` to the vector.
 * Compile-time error if `data` does not point to a compatible element type.
 * @warning `data` must not point into `v`'s own buffer; use kv_splice() to
 * append a vector to itself.
 */
#define kv_concat_len(v, data, len)                                                                 \
  ({                                                                                                \
    const kvec_typeof((v).items[0]) *kvec_src_ = (data);                                            \
    const size_t kvec_n_ = (size_t) (len);                                                          \
    if (kvec_n_ > 0) {                                                                              \
      kv_ensure_space(v, kvec_n_);                                                                  \
      kvec_bound_((v).items != NULL);                                                               \
      memcpy((void *) ((v).items + (v).size), (const void *) kvec_src_, kvec_bytes_((v), kvec_n_)); \
      (v).size = kv_checked_add((v).size, kvec_n_);                                                 \
    }                                                                                               \
    (v).size;                                                                                       \
  })

/**
 * @brief Appends a null-terminated string to a character vector.
 * @param[in,out] v   The vector.
 * @param[in]     str Null-terminated string to append.
 */
#define kv_concat(v, str) kv_concat_len(v, str, strlen(str))

/**
 * @brief Appends all elements of vector `v0` to `v1`.
 * Compile-time error if the element types differ.
 * Appending a vector to itself (`kv_splice(v, v)`) is supported.
 * NOTE: (v0).items is intentionally re-read AFTER kv_ensure_space():
 * when v1 == v0 the ensure may reallocate, and a cached pointer would dangle.
 */
#define kv_splice(v1, v0)                                                                   \
  ({                                                                                        \
    KVEC_ASSERT_SAME_TYPE((v1).items[0], (v0).items[0], "kv_splice: element types differ"); \
    size_t kv_v0_size_ = (v0).size;                                                         \
    if (kv_v0_size_ > 0) {                                                                  \
      kv_ensure_space(v1, kv_v0_size_);                                                     \
      /* ensure space for >0 elems implies a live buffer */                                 \
      kvec_bound_((v1).items != NULL);                                                      \
      memcpy((void *) ((v1).items + (v1).size), (const void *) (v0).items,                  \
             kvec_bytes_((v1), kv_v0_size_));                                               \
      (v1).size = kv_checked_add((v1).size, kv_v0_size_);                                   \
    }                                                                                       \
    (v1).size;                                                                              \
  })

/**
 * @brief Gets a pointer to the next free slot, expanding capacity if needed.
 * @return Pointer to the new, uninitialized slot (size is incremented).
 */
#define kv_pushp(v)                                           \
  ({                                                          \
    if ((v).size == (v).capacity) {                           \
      kv_resize_full(v);                                      \
    }                                                         \
    kvec_typeof((v).items) kvec_slot_ = (v).items + (v).size; \
    (v).size += 1;                                            \
    kvec_slot_;                                               \
  })

/**
 * @brief Pushes an element onto the vector, expanding if needed.
 * The value passes through a temporary of the element type, so pushing an
 * incompatible value (e.g. the wrong pointer type) is a compile-time error;
 * narrowing conversions warn under -Wconversion.
 * @return The pushed value.
 */
#define kv_push(v, x)                          \
  ({                                           \
    kvec_typeof((v).items[0]) kvec_val_ = (x); \
    *kv_pushp(v) = kvec_val_;                  \
    kvec_val_;                                 \
  })

/**
 * @brief Pushes one or more values, e.g. `kv_push_all(v, 1, 2, 3)`.
 * Every argument is type-checked through a temporary array of the element
 * type. Requires at least one value.
 */
#define kv_push_all(v, ...)                                       \
  ({                                                              \
    const kvec_typeof((v).items[0]) kvec_vals_[] = {__VA_ARGS__}; \
    kv_concat_len(v, kvec_vals_, KV_ARRAY_SIZE(kvec_vals_));      \
    (v).size;                                                     \
  })

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
 * @brief Dynamic element access. Expands capacity and size if the index is
 * out of bounds (new slots are uninitialized).
 * @return L-value reference to the element at index `i`.
 * @warn Heap-only: DO NOT use with `kvec_withinit_t`.
 */
#define kv_a(v, i)                                               \
  (*({                                                           \
    const size_t kvec_i_ = (size_t) (i);                         \
    if ((v).capacity <= kvec_i_) {                               \
      kv_resize(v, kv_roundup_size(kv_checked_add(kvec_i_, 1))); \
      (v).size = kvec_i_ + 1;                                    \
    } else if ((v).size <= kvec_i_) {                            \
      (v).size = kvec_i_ + 1;                                    \
    }                                                            \
    &(v).items[kvec_i_];                                         \
  }))

/**
 * @brief Unordered removal: overwrites element `i` with the last element and
 * shrinks the size by one. O(1), but does not preserve order.
 */
#define kv_remove_swap(v, i)                      \
  ({                                              \
    const size_t kvec_i_ = (size_t) (i);          \
    kvec_bound_(kvec_i_ < (v).size);              \
    (v).items[kvec_i_] = (v).items[(v).size - 1]; \
    (v).size -= 1;                                \
  })

/**
 * @brief Removes `n` elements starting at index `i` by shifting elements left.
 * @param i Index to start removing from.
 * @param n Number of elements to remove.
 */
#define kv_shift(v, i, n)                                                                 \
  ({                                                                                      \
    const size_t kvec_i_ = (size_t) (i);                                                  \
    const size_t kvec_n_ = (size_t) (n);                                                  \
    kvec_bound_(kvec_n_ <= (v).size);                                                     \
    kvec_bound_(kvec_i_ <= (v).size - kvec_n_);                                           \
    if (kvec_i_ + kvec_n_ < (v).size) {                                                   \
      memmove((void *) &(v).items[kvec_i_], (const void *) &(v).items[kvec_i_ + kvec_n_], \
              kvec_bytes_((v), (v).size - kvec_i_ - kvec_n_));                            \
    }                                                                                     \
    (v).size -= kvec_n_;                                                                  \
  })

/**
 * @brief Iterates over a vector: `kv_foreach (v, it) { ... *it ... }`.
 * `it` is declared by the macro as a pointer to the element type.
 * @warning Do not modify the vector inside the loop; pushing may reallocate
 * the buffer and dangle `it`.
 */
#define kv_foreach(v, it)                                                                     \
  for (kvec_typeof((v).items[0]) * (it) = (v).items,                                          \
                                   *kvec_end_ = (v).items ? (v).items + (v).size : (v).items; \
       (it) != kvec_end_; ++(it))

/**
 * @brief Iterates over the first min((v).size, n) elements of a vector:
 * `kv_forN (v, n, it) { ... *it ... }`.
 * `it` is declared by the macro as a pointer to the element type.
 * `v` and `n` are evaluated only at loop entry, but more than once;
 * pass plain variables. `n` must be non-negative.
 * @warning Do not modify the vector inside the loop; any size change
 * invalidates the cached end pointer, and pushing may dangle `it`.
 */
#define kv_forN(v, n, it)                                                                               \
  for (kvec_typeof((v).items[0]) *                                                                      \
           (it) = (v).items,                                                                            \
           *KVEC_UNIQ(kvec_end_) =                                                                      \
               (v).items ? (v).items + ((size_t) (n) < (v).size ? (size_t) (n) : (v).size) : (v).items; \
       (it) != kvec_end_; ++(it))

/**
 * @brief Reverse iteration by index:
 * `kv_foreach_rev (v, i) { ... kv_A(v, i) ... }`.
 */
#define kv_foreach_rev(v, i) for (size_t(i) = (v).size; (i)-- > 0;)

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

/**
 *  @brief Named inline-vector type
 *  Example: `KVI_TYPEDEF(int, 16, int_vec16);`
 */
#define KVI_TYPEDEF(type, init_size, name) typedef kvec_withinit_t(type, init_size) name

// NOLINTEND

/**
 * @brief Static initializer for an inline vector.
 * @param v The declared vector variable.
 */
#define KVI_INITIAL_VALUE(v) {.size = 0, .capacity = KV_ARRAY_SIZE((v).init_array), .items = (v).init_array}

/**
 * @brief Initialize vector with its preallocated array.
 * @param[out] v Vector to initialize.
 */
#define kvi_init(v) ((v).capacity = KV_ARRAY_SIZE((v).init_array), (v).size = 0, (v).items = (v).init_array)

/**
 * @brief Resizes an inline vector, handling the stack <-> heap transitions.
 * Aborts if the new capacity would be smaller than the current size; the
 * capacity is never reduced below the inline array size.
 */
#define kvi_resize(v, s)                                                                                 \
  ({                                                                                                     \
    const size_t kvec_init_cap_ = KV_ARRAY_SIZE((v).init_array);                                         \
    const size_t kvec_want_ = (size_t) (s);                                                              \
    const size_t kvec_new_cap_ = (kvec_want_ > kvec_init_cap_) ? kvec_want_ : kvec_init_cap_;            \
    kvec_bound_((v).size <= kvec_new_cap_);                                                              \
    if (kvec_new_cap_ == kvec_init_cap_) {                                                               \
      /* Move back down to the inline array. */                                                          \
      if ((v).items != (v).init_array) {                                                                 \
        if ((v).size > 0) {                                                                              \
          memcpy((void *) (v).init_array, (const void *) (v).items, kvec_bytes_((v), (v).size));         \
        }                                                                                                \
        if ((v).items != NULL) {                                                                         \
          KVEC_FREE((void *) (v).items);                                                                 \
        }                                                                                                \
        (v).items = (v).init_array;                                                                      \
      }                                                                                                  \
    } else if ((v).items == (v).init_array || (v).items == NULL) {                                       \
      /* Move from the inline array to a fresh heap buffer. */                                           \
      kvec_typeof((v).items) kvec_new_ =                                                                 \
          (kvec_typeof((v).items)) kv_malloc_safe(kvec_bytes_((v), kvec_new_cap_));                      \
      if ((v).size > 0) {                                                                                \
        memcpy((void *) kvec_new_, (const void *) (v).items, kvec_bytes_((v), (v).size));                \
      }                                                                                                  \
      (v).items = kvec_new_;                                                                             \
    } else {                                                                                             \
      /* Already on the heap. */                                                                         \
      (v).items =                                                                                        \
          (kvec_typeof((v).items)) kv_realloc_safe((void *) (v).items, kvec_bytes_((v), kvec_new_cap_)); \
    }                                                                                                    \
    (v).capacity = kvec_new_cap_;                                                                        \
    (v).capacity;                                                                                        \
  })

/**
 * @brief Doubles the capacity of an inline vector when it is full.
 *
 * `KV_ARRAY_SIZE((v).init_array)` is the minimal capacity of this vector.
 * Thus when vector is full capacity may not be zero and it is safe
 * not to bother with checking whether v.capacity is 0. But now
 * capacity is not guaranteed to have size that is a power of 2, it is
 * hard to fix this here and is not very necessary if users will use
 * 2^x initial array size.
 */
#define kvi_resize_full(v) kvi_resize((v), (v).capacity ? kv_checked_add((v).capacity, (v).capacity) : 8)

/** @brief Ensures a total capacity (static + dynamic) of at least `n` elements. */
#define kvi_reserve(v, n)                         \
  ({                                              \
    const size_t kvec_need_ = (size_t) (n);       \
    if ((v).capacity < kvec_need_) {              \
      kvi_resize(v, kv_roundup_size(kvec_need_)); \
    }                                             \
    (v).capacity;                                 \
  })

/** @brief Ensures capacity for at least `len` more elements. */
#define kvi_ensure_more_space(v, len)                                   \
  ({                                                                    \
    const size_t kvec_need_ = kv_checked_add((v).size, (size_t) (len)); \
    if ((v).capacity < kvec_need_) {                                    \
      kvi_resize(v, kv_roundup_size(kvec_need_));                       \
    }                                                                   \
    (v).capacity;                                                       \
  })

/**
 * @brief Appends `len` elements from `data` to an inline vector.
 * Compile-time error if `data` does not point to a compatible element type.
 * @warning `data` must not point into `v`'s own buffer; use kvi_splice() to
 * append a vector to itself.
 */
#define kvi_concat_len(v, data, len)                                                                \
  ({                                                                                                \
    const kvec_typeof((v).items[0]) *kvec_src_ = (data);                                            \
    const size_t kvec_n_ = (size_t) (len);                                                          \
    if (kvec_n_ > 0) {                                                                              \
      kvi_ensure_more_space(v, kvec_n_);                                                            \
      memcpy((void *) ((v).items + (v).size), (const void *) kvec_src_, kvec_bytes_((v), kvec_n_)); \
      (v).size = kv_checked_add((v).size, kvec_n_);                                                 \
    }                                                                                               \
    (v).size;                                                                                       \
  })

/**
 * @brief Appends a null-terminated string to a character inline vector.
 * @param[in]     str Null-terminated string.
 */
#define kvi_concat(v, str) kvi_concat_len(v, (str), strlen(str))

/**
 * @brief Appends all elements of `v0` to inline vector `v1`.
 * `v0` may be a standard or an inline vector; element types must match.
 */
#define kvi_splice(v1, v0)                                                                                \
  ({                                                                                                      \
    KVEC_ASSERT_SAME_TYPE((v1).items[0], (v0).items[0], "kvi_splice: element types differ");              \
    if ((v0).size > 0) {                                                                                  \
      kvi_ensure_more_space((v1), (v0).size);                                                             \
      memcpy((void *) ((v1).items + (v1).size), (const void *) (v0).items, kvec_bytes_((v1), (v0).size)); \
      (v1).size = kv_checked_add((v1).size, (v0).size);                                                   \
    }                                                                                                     \
    (v1).size;                                                                                            \
  })

/** @brief Gets a pointer to the next free slot, expanding if needed. */
#define kvi_pushp(v)                                          \
  ({                                                          \
    if ((v).size == (v).capacity) {                           \
      kvi_resize_full(v);                                     \
    }                                                         \
    kvec_typeof((v).items) kvec_slot_ = (v).items + (v).size; \
    (v).size += 1;                                            \
    kvec_slot_;                                               \
  })

/** @brief Pushes an element onto an inline vector (type checked). */
#define kvi_push(v, x)                         \
  ({                                           \
    kvec_typeof((v).items[0]) kvec_val_ = (x); \
    *kvi_pushp(v) = kvec_val_;                 \
    kvec_val_;                                 \
  })

/**
 * @brief Copies @p `v0` (standard or inline vector) into inline vector @p `v1`.
 * Compile-time error if the element types differ.
 */
#define kvi_copy(v1, v0)                                                                   \
  ({                                                                                       \
    KVEC_ASSERT_SAME_TYPE((v1).items[0], (v0).items[0], "kvi_copy: element types differ"); \
    const kvec_typeof((v1).items[0]) *kvec_src_ = (v0).items;                              \
    const size_t kvec_n_ = (v0).size;                                                      \
    if (kvec_n_ > 0 && &(v1).size != &(v0).size) {                                         \
      if ((v1).capacity < kvec_n_) {                                                       \
        kvi_resize(v1, kvec_n_);                                                           \
      }                                                                                    \
      memcpy((void *) (v1).items, (const void *) kvec_src_, kvec_bytes_((v1), kvec_n_));   \
    }                                                                                      \
    (v1).size = kvec_n_;                                                                   \
    (v1).size;                                                                             \
  })

/** @brief Pushes one or more values onto an inline vector (type checked). */
#define kvi_push_all(v, ...)                                      \
  ({                                                              \
    const kvec_typeof((v).items[0]) kvec_vals_[] = {__VA_ARGS__}; \
    kvi_concat_len(v, kvec_vals_, KV_ARRAY_SIZE(kvec_vals_));     \
    (v).size;                                                     \
  })

/**
 * @brief Destroys an inline vector.
 * Frees the heap buffer only if the vector had outgrown its inline array.
 * The vector is reset to its initial (inline) state and can be reused.
 */
#define kvi_destroy(v)                 \
  do {                                 \
    if ((v).items != (v).init_array) { \
      KVEC_FREE_CLEAR((v).items);      \
    }                                  \
    kvi_init(v);                       \
  } while (0)

#endif  // PORTABLE_KVEC_H
