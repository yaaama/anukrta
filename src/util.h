/**
 * @file util.h
 * Utility functions/macros used throughout the codebase.
 */

#ifndef AK_UTIL_H
#define AK_UTIL_H

#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* IWYU pragma: keep */
#include <unistd.h>

#define AK_TOSTRING(s) #s
#define AK_STRINGIFY(s) AK_TOSTRING(s)

#define AK_GLUE(a, b) a##b
#define AK_JOIN(a, b) AK_GLUE(a, b)

#define AK_UNIQ_T(x, uniq) AK_JOIN(__unique_prefix_, AK_JOIN(x, uniq))
#define AK_UNIQ __COUNTER__

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#ifndef __has_attribute
#  define __has_attribute(x) 0
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#  error "We require GNUisms to build!"
#endif

/**
 * @name Compilation warning controls
 * Compiler warnings control macros
 * @{
 */

#define AK_PRAGMA(x) _Pragma(#x)

/* Base state controls */
#define AK_WARNING_PUSH AK_PRAGMA(GCC diagnostic push)
#define AK_WARNING_POP AK_PRAGMA(GCC diagnostic pop)
#define AK_WARNING_IGNORE(warning_flag) AK_PRAGMA(GCC diagnostic ignored warning_flag)

/* Specific Warning Disablers */
#define AK_DISABLE_WARNING_UNUSED_VARIABLE AK_WARNING_IGNORE("-Wunused-variable")
#define AK_DISABLE_WARNING_UNUSED_PARAMETER AK_WARNING_IGNORE("-Wunused-parameter")
#define AK_DISABLE_WARNING_UNUSED_FUNCTION AK_WARNING_IGNORE("-Wunused-function")
#define AK_DISABLE_WARNING_UNUSED_CONST_VAR AK_WARNING_IGNORE("-Wunused-const-variable")

/* Safely combine multiple ignores without pushing to the stack multiple times */
#define AK_DISABLE_WARNING_UNUSED_ALL \
  AK_DISABLE_WARNING_UNUSED_CONST_VAR \
  AK_DISABLE_WARNING_UNUSED_FUNCTION  \
  AK_DISABLE_WARNING_UNUSED_PARAMETER \
  AK_DISABLE_WARNING_UNUSED_VARIABLE

#define AK_DISABLE_WARNING_SIGN_COMPARE AK_WARNING_IGNORE("-Wsign-compare")
#define AK_DISABLE_WARNING_SHADOW AK_WARNING_IGNORE("-Wshadow")
#define AK_DISABLE_WARNING_CONVERSION AK_WARNING_IGNORE("-Wconversion")
#define AK_DISABLE_WARNING_STRICT_ALIASING AK_WARNING_IGNORE("-Wstrict-aliasing")
#define AK_DISABLE_WARNING_FALLTHROUGH AK_WARNING_IGNORE("-Wimplicit-fallthrough")
#define AK_DISABLE_WARNING_PADDED AK_WARNING_IGNORE("-Wpadded")

/** @} */  // END COMPILER WARNING CONTROLS

/**
 * @name Function/Variable Attributes
 * Function and Variable Attributes
 * @{
 */

/**
 * @def AK_UNUSED
 * Suppresses compiler warnings about unused variables, parameters, or functions.
 *
 * Useful when a variable is only used in certain build configurations (e.g., `#ifdef DEBUG`)
 * or for required function signatures where not all parameters are needed.
 *
 * @par Example Usage:
 * @code
 * void event_handler(int event_id, void* AK_UNUSED context) {
 *    printf("Event: %d\n", event_id);
 * }
 * @endcode
 */
#if __has_attribute(unused)
#  define AK_UNUSED __attribute__((unused))
#endif

/**
 * @def AK_FLAG_ENUM
 * Tells the compiler and debugger that this enum represents bitwise flags.
 *
 * Normally, if you perform a bitwise OR on two enum values, the compiler might
 * warn that the resulting integer is not a valid predefined value of that enum.
 * This attribute suppresses those warnings and allows debuggers (like LLDB/GDB)
 * to print the value cleanly as a combination of flags (e.g., `READ | WRITE`)
 * instead of a raw integer (e.g., `3`).
 *
 * @par Example Usage:
 * @code
 * enum AK_FLAG_ENUM FilePermissions {
 *     PERM_NONE  = 0,
 *     PERM_READ  = 1 << 0,
 *     PERM_WRITE = 1 << 1,
 *     PERM_EXEC  = 1 << 2,
 *     PERM_ALL   = PERM_READ | PERM_WRITE | PERM_EXEC
 * };
 *
 * // The compiler knows this is perfectly legal:
 * enum FilePermissions my_perms = PERM_READ | PERM_WRITE;
 * @endcode
 */
#if __has_attribute(flag_enum)
#  define AK_FLAG_ENUM __attribute__((flag_enum))
#else
#  define AK_FLAG_ENUM
#endif

/**
 * @def AK_PREF_TYPE
 * @brief Specifies the intended original type of a variable or bit-field for tooling.
 *
 * When packing structs to save memory, it is common to store an `enum` inside a
 * smaller integer type (like `uint8_t`) or a bit-field. However, doing this loses
 * the type information, meaning debuggers will just show a raw number instead of
 * the enum name. This attribute restores that context.
 *
 * @param x The original type (e.g., the enum type) that this variable represents.
 *
 * @par Example Usage:
 * @code
 * enum ConnectionState {
 *     STATE_DISCONNECTED,
 *     STATE_CONNECTING,
 *     STATE_CONNECTED
 * };
 *
 * struct NetworkSocket {
 *     // We use a 4-bit field to save space, but we tell the compiler/debugger
 *     // to treat this value as an 'enum ConnectionState' when inspecting it.
 *     unsigned int state AK_PREF_TYPE(enum ConnectionState) : 4;
 *
 *     // Similarly, for fixed-width integers:
 *     uint8_t prev_state AK_PREF_TYPE(enum ConnectionState);
 * };
 * @endcode
 */
#if __has_attribute(preferred_type)
#  define AK_PREF_TYPE(x) __attribute__((preferred_type(x)))
#else
#  define AK_PREF_TYPE(x)
#endif

/**
 * @def AK_SIZED_BY
 * @brief Associates a pointer or flexible array member with its byte-size field.
 *
 * @param x The name of the struct member that holds the size in bytes.
 *
 * @par Example Usage:
 * @code
 * struct StringView {
 *     size_t length;
 *     const char* text AK_SIZED_BY(length);
 * };
 *
 * // Usage within a function:
 * void process_buffer(void* data AK_SIZED_BY(buffer_size), size_t buffer_size) {
 *    // SAFE because it is within bounds
 *    for (size_t i = 0; i < buffer_size; i++) {
 *        uint8_t byte = ((uint8_t*)data)[i];
 *    }
 *
 *    // TRAP / WARNING: The compiler knows this is an out-of-bounds
 *    uint8_t bad_byte = ((uint8_t*)data)[buffer_size + 1];
 * }
 * @endcode
 */
#if __has_attribute(sized_by)
#  define AK_SIZED_BY(x) __attribute__((sized_by(x)))
#else
#  define AK_SIZED_BY(x)
#endif

/**
 * @def AK_COUNTED_BY
 * @brief Associates a pointer or flexible array member with its element count.
 *
 * @param x The name of the struct member that holds the element count.
 *
 * @par Example Usage:
 * @code
 * struct EmployeeDirectory {
 *     int num_employees;
 *     struct Employee* employees AK_COUNTED_BY(num_employees);
 * };
 *
 * // Usage inside of a function:
 * void sort_items(int* array AK_COUNTED_BY(count), size_t count) {
 *    // The compiler knows 'array' has exactly 'count' elements.
 * }
 * @endcode
 */
#if __has_attribute(counted_by)
#  define AK_COUNTED_BY(x) __attribute__((counted_by(x)))
#else
#  define AK_COUNTED_BY(x)
#endif

/**
 * @def AK_ALWAYS_INLINE
 * Forces the compiler to inline the function, regardless of optimization limits.
 *
 * Bypasses the compiler's normal cost-benefit analysis for inlining. Use sparingly,
 * typically for very small, performance-critical functions.
 *
 * @par Example Usage:
 * @code
 * static AK_ALWAYS_INLINE int get_fast_multiplier(int base) {
 *    return base << 2;
 * }
 * @endcode
 */
#ifdef NDEBUG
#  if __has_attribute(always_inline)
#    define AK_ALWAYS_INLINE inline __attribute__((always_inline))
#  else
#    define AK_ALWAYS_INLINE inline
#  endif
#else
#  define AK_ALWAYS_INLINE inline
#endif

/**
 * @def AK_NEVER_INLINE
 * Forces the compiler to never inline the function.
 *
 * @par Example Usage:
 * @code
 * static AK_NEVER_INLINE int oom_err(char *message) {
 *    printf("%s\n", message);
 *    exit(1);
 * }
 * @endcode
 */
#if __has_attribute(noinline)
#  define AK_NEVER_INLINE __attribute__((noinline))
#else
#  define AK_NEVER_INLINE
#endif

/**
 * @def AK_FLATTEN
 * Forces the compiler to inline every function called WITHIN this function.
 *
 * Useful for performance-critical wrapper functions where you want to eliminate
 * all function call overhead inside the body of this specific function.
 *
 * @par Example Usage:
 * @code
 * void execute_tight_loop(void) AK_FLATTEN {
 *    step_one(); // Will be inlined
 *    step_two(); // Will be inlined
 * }
 * @endcode
 */
#if __has_attribute(flatten)
#  define AK_FLATTEN __attribute__((flatten))
#else
#  define AK_FLATTEN
#endif

/**
 * @def AK_HOT_FUNC
 * Marks a function as a "hot spot" (executed very frequently).
 *
 * Instructs the compiler to optimize this function heavily for speed, and informs
 * branch predictors that calls to this function are highly likely to happen.
 *
 * @par Example Usage:
 * @code
 * void process_audio_sample(float sample) AK_HOT_FUNC;
 * @endcode
 */
#if __has_attribute(hot)
#  define AK_HOT_FUNC __attribute__((hot))
#else
#  define AK_HOT_FUNC
#endif

/**
 * @def AK_COLD_FUNC
 * Marks a function as "cold" (rarely executed).
 *
 * Instructs the compiler to optimize this function for size rather than speed,
 * and to move its code out of the main execution path to improve CPU instruction
 * caching for the hot code. Ideal for error handling.
 *
 * @par Example Usage:
 * @code
 * void handle_out_of_memory(void) AK_COLD_FUNC;
 * @endcode
 */
#if __has_attribute(cold)
#  define AK_COLD_FUNC __attribute__((cold))
#else
#  define AK_COLD_FUNC
#endif

/**
 * @def AK_PURE
 * Marks a function as "pure", meaning it has no side effects.
 *
 * The function's return value must depend ONLY on its parameters and/or global
 * variables. It must not modify global state or perform I/O. This allows the
 * compiler to optimize away redundant calls (e.g., in loops).
 *
 * @par Example Usage:
 * @code
 * int string_hash(const char* str) AK_PURE;
 * @endcode
 */
#if __has_attribute(pure)
#  define AK_PURE __attribute__((pure))
#else
#  define AK_PURE
#endif

/**
 * @def AK_CONST
 * Marks a function as "const", a stricter version of pure.
 *
 * The function's return value must depend ONLY on its parameters. It cannot
 * even read global variables or dereference pointers to global memory.
 * Mathematical functions like `abs()` or `square()` are good examples.
 *
 * @par Example Usage:
 * @code
 * int square(int x) AK_CONST;
 * @endcode
 */
#if __has_attribute(const)
#  define AK_CONST __attribute__((const))
#else
#  define AK_CONST
#endif

/**
 * @def AK_NONNULL_ALL
 * Specifies that the compiler should warn if ANY pointer argument is NULL.
 *
 * Applies to all pointer arguments in the function signature. Enables
 * aggressive optimizations by assuming pointers are always valid.
 *
 * @par Example Usage:
 * @code
 * void process_data(const char* input, char* output) AK_NONNULL_ALL;
 * @endcode
 */

/**
 * @def AK_NONNULL_ARG
 * Specifies that specific pointer arguments must not be NULL.
 *
 * @param ... A comma-separated list of 1-based parameter indices.
 *
 * @par Example Usage (Arguments 1 and 3 cannot be NULL):
 * @code
 * void safe_memcpy(void* dest, size_t len, const void* src) AK_NONNULL_ARG(1, 3);
 * @endcode
 */
#if __has_attribute(nonnull)
#  define AK_NONNULL_ALL __attribute__((nonnull))
#  define AK_NONNULL_ARG(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#  define AK_NONNULL_ALL
#  define AK_NONNULL_ARG(...)
#endif

/**
 * @def AK_RET_NONNULL
 * Tells the compiler that the function will return a non-null value.
 */
#if __has_attribute(returns_nonnull)
#  define AK_RET_NONNULL __attribute__((returns_nonnull))
#else
#  define AK_RET_NONNULL
#endif

/**
 * @def AK_NO_DISCARD
 * Emits a compiler warning if the caller ignores the return value.
 *
 * Highly recommended for functions that allocate memory, return error codes,
 * or acquire locks, where ignoring the result leads to memory leaks or bugs.
 *
 * @par Example Usage:
 * @code
 * int init_hardware_subsystem(void) AK_NO_DISCARD;
 * @endcode
 */
#if __has_attribute(warn_unused_result)
#  define AK_NO_DISCARD __attribute__((warn_unused_result))
#else
#  define AK_NO_DISCARD
#endif

/**
 * @def AK_NO_RETURN
 * Indicates that the function will never return to its caller.
 *
 * Used for functions that terminate the program (e.g., `exit()`), enter an
 * infinite loop, or throw longjmps/exceptions. Suppresses "reached end of
 * non-void function" warnings.
 *
 * @par Example Usage:
 * @code
 * AK_NO_RETURN void fatal_panic(const char* reason);
 * @endcode
 */
#if __has_attribute(noreturn)
#  define AK_NO_RETURN __attribute__((noreturn))
#else
#  define AK_NO_RETURN
#endif

/**
 * @def AK_MALLOC
 * Tells the compiler that the function returns a newly allocated pointer.
 *
 * Asserts that the returned pointer cannot alias (overlap) with any other
 * valid pointer in the program. This allows the compiler to perform better
 * alias analysis and optimization.
 *
 * @par Example Usage:
 * @code
 * void* custom_allocator(size_t size) AK_MALLOC;
 * @endcode
 */
#if __has_attribute(malloc)
#  define AK_MALLOC __attribute__((malloc))
#else
#  define AK_MALLOC
#endif

/**
 * @def AK_ALLOC
 * Informs the compiler of the allocation size based on 1 or 2 arguments.
 *
 * @param ... A single 1-based index (like malloc), or TWO 1-based indices
 *           (like calloc) where the total size is (arg1 * arg2).
 *
 * @par Example Usage:
 * @code
 * AK_ALLOC_SZ(1)    void* custom_malloc(size_t size);
 * AK_ALLOC_SZ(1, 2) void* custom_calloc(size_t count, size_t size);
 * @endcode
 */
#if __has_attribute(alloc_size)
#  define AK_ALLOC_SZ(...) __attribute__((alloc_size(__VA_ARGS__)))
#else
#  define AK_ALLOC_SZ(...)
#endif

/* NOTE: 2 arg attribute `malloc(func,i)` is only supported by gcc, not clang */
#if defined(__GNUC__) && (__GNUC__ >= 11)

/**
 * @def AK_DEALLOCATOR
 * Associates an allocation function with its specific deallocation function.
 *
 * This extended version of the malloc attribute tells the compiler's static
 * analyzer exactly how the allocated memory should be freed. This allows the
 * compiler to detect memory leaks, use-after-free bugs, and mismatched
 * allocator/deallocator pairs (e.g., allocating with `custom_malloc` but
 * accidentally freeing with the standard `free()`).
 *
 * @param deallocator The name of the function used to free the returned pointer.
 * @param ptr_index   The 1-based index of the argument in the deallocator
 *                   function that receives the pointer to be freed.
 *
 * @par Example Usage:
 * @code
 * // Forward declaration of the deallocator is required first
 * void custom_free(void* ptr);
 *
 * // Tell the compiler that custom_malloc pairs with custom_free,
 * // and the pointer is passed as the 1st argument to custom_free.
 * AK_DEALLOCATOR(custom_free, 1) void* custom_malloc(size_t size);
 *
 * void test_function() {
 *    void* ptr = custom_malloc(128);
 *    free(ptr); // Compiler warning: 'ptr' should have been freed with 'custom_free'
 * }              // Compiler warning if not freed at all: memory leak detected
 * @endcode
 */
#  define AK_DEALLOCATOR(dealloc, idx) __attribute__((malloc(dealloc, idx)))
#else
#  define AK_DEALLOCATOR(dealloc, idx)
#endif

/**
 * @def AK_PRINTF
 * Enables printf-style format string type checking by the compiler.
 *
 * @param x The 1-based index of the format string parameter.
 * @param y The 1-based index of the first variadic argument (`...`).
 *
 * @par Example Usage:
 * @code
 * // Arg 1 is format string, Arg 2 is the first variadic argument
 * void log_message(const char* fmt, ...) AK_PRINTF(1, 2);
 * @endcode
 */
#if __has_attribute(format)
#  define AK_PRINTF(x, y) __attribute__((format(printf, x, y)))
#else
#  define AK_PRINTF(x, y)
#endif

/**
 * @name Cleanup Macros (RAII)
 * Helper macros to automatically free/close resources when out of scope.
 * @{
 */

#if __has_attribute(cleanup)
/**
 * @def AK_CLEANUP
 * Automatically cleanup allocated types.
 */
#  define AK_CLEANUP(func) __attribute__((cleanup(func)))
#else
#  define AK_CLEANUP(func)
#endif

/**
 * Dummy function to ensure we check pointers during the close.
 */
static AK_ALWAYS_INLINE AK_NO_DISCARD void *ak__ptr_must_check (void *p) {
  return p;
}

/**
 * @def AK_DEFINE_AUTO
 * Creates the wrapper function the compiler actually calls.
 *
 * @param Name  The short name to use in AK_AUTO (e.g., free, fd, file).
 * @param type  The type of the object (e.g., void*, int, FILE*).
 * @param logic The statement to free the object.
 */
#define AK_DEFINE_AUTO(name, type, logic)                 \
  static AK_ALWAYS_INLINE void ak__auto_##name(void *p) { \
    type _T = *(type *) p;                                \
    logic;                                                \
  }

/**
 * @def AK_AUTO
 * The attribute to put on a variable to clean up later.
 * Usage: AK_AUTO(free) char* text = malloc(10);
 */
#define AK_AUTO(name) AK_CLEANUP(ak__auto_##name)

/**
 * @def no_free_ptr
 * Prevent automatic cleanup of pointer.
 */
#define ak_take_ptr(p)                   \
  ((__typeof__(p)) ak__ptr_must_check(({ \
    __typeof__(p) __val = (p);           \
    (p) = NULL;                          \
    __val;                               \
  })))

/**
 * @def ak_return_ptr
 * Wrapper around ak_take_ptr for return statements.
 */
#define ak_return_ptr(p) return ak_take_ptr(p)

/**
 * @def ak_take_fd
 * Prevent automatic cleanup of a file descriptor (taking ownership).
 * Because FDs are integers, we invalidate them with -1, not NULL.
 */
#define ak_take_fd(fd)           \
  ({                             \
    __typeof__(fd) __val = (fd); \
    (fd) = -1;                   \
    __val;                       \
  })

/** Free an allocated pointer (e.g., malloc, calloc) */
AK_DEFINE_AUTO(free, void *, if (_T) free(_T))

/** Close a UNIX file descriptor */
AK_DEFINE_AUTO(fd, int, if (_T >= 0) close(_T))

/** Close a DIR* stream */
AK_DEFINE_AUTO(dir, DIR *, if (_T) closedir(_T))

/** Close a FILE* stream */
AK_DEFINE_AUTO(file, FILE *, if (_T) fclose(_T))

/** @} */  // end cleanup macros

/**
 * @name BitMacros Flag Macros
 * Macros for safe bitflag manipulation.
 * @{
 */

/**
 * @def ak_updated_flag
 * Returns a NEW mask with flag(s) conditionally set or cleared based on 'cond'.
 * @note Does NOT modify the original mask in place.
 */
#define ak_updated_flag(orig, flag, cond) ((cond) ? ((orig) | (flag)) : ((orig) & ~(flag)))

/**
 * @def ak_set_flag_if
 * Conditionally sets or clears a flag in the bitmask in place.
 *
 * ```c
 * // Sets STATUS_RUNNING if 'is_moving' is true, clears it if false.
 * ak_set_flag_if(player_state, STATUS_RUNNING, is_moving);
 * ```
 */
#define ak_set_flag_if(mask, flag, cond) ((mask) = ak_updated_flag(mask, flag, cond))

/**
 * @def ak_flag_all
 * Checks if ALL specified flags are set.
 * @note If flag is 0, this will return true.
 * ```c
 * if (ak_flag_all(player_state, STATUS_RUNNING | STATUS_POISONED)) {
 * printf("Player is losing health fast!\n");
 * }
 * ```
 */
#define ak_flag_all(mask, flag) ((~(mask) & (flag)) == (0))

/**
 * @def ak_flag_has
 * Macro for when a single flag is being tested.
 */

#define ak_flag_has(mask, flag) ak_flag_all(mask, flag)

/**
 * @def ak_flag_any
 * Checks if ANY of the specified flags are set.
 */
#define ak_flag_any(mask, flag) (((mask) & (flag)) != 0)

/** @} */  // End BitMacros group

/**
 * @name Utility Macros
 * Generally useful macros.
 * @{
 */

#define VOID_0 ((void) 0)

/**
 * @def AK_ARRAY_SIZE
 * Calculate the length of a C array
 */
#define AK_ARRAY_SIZE(x)                                                          \
  (__builtin_choose_expr(!__builtin_types_compatible_p(typeof(x), typeof(&*(x))), \
                         sizeof(x) / sizeof((x)[0]), VOID_0))

/**
 * @def AK_ARRAY_LAST_ENTRY
 * Get last array entry.
 *
 * @note This should be called with a real array.
 * @warning Calling this with a pointer is an *error*.
 */
#define AK_ARRAY_LAST_ENTRY(array) (array)[AK_ARRAY_SIZE(array) - 1]

/**
 * @def ak_memzero_arr
 * Zero out array of type `type` */
#define ak_memzero_arr(pointer, count, type) memset((pointer), 0, (count) * sizeof(type))
/**
 * @def ak_memzero_sz
 * Zero out n number of bytes. */
#define ak_memzero_sz(pointer, sz) memset((pointer), 0, sz)

/** @name Conditionals
 * Helps in conditional logic.
 * @{
 */

#if __has_builtin(__builtin_expect)
/**
 * @def ak_likely
 * Hint to compiler that the branch is most likely *TRUE*.
 * @note The `!!` converts expression `x` into a boolean value. The first `!`
 * negates it into a boolean, and then the second `!` will return it back to its
 * actual value. This is essential due to the fact that `__builtin_expect(x, y)`
 * instructs to the compiler that `x` is exactly equal to `y`.
 * E.g.
 * ```c
 * int x = 42;
 * if (__builtin_expect(x, 1)) { ... }
 * ```
 * Would not work in this case, however when we do `!!(42)`, it evaluates to `1`
 * (as it is non-zero/non-null).
 */
#  define ak_likely(x) __builtin_expect(!!(x), 1)

/**
 * @def ak_unlikely
 * Hint to the compiler the condition is most likely *FALSE*.
 */
#  define ak_unlikely(x) __builtin_expect(!!(x), 0)

#else
#  define ak_likely(x) (x)
#  define ak_unlikely(x) (x)
#endif

/* clang-format off */
#define CASE_F_1(X) case X:
#define CASE_F_2(X, ...) case X: CASE_F_1(__VA_ARGS__)
#define CASE_F_3(X, ...) case X: CASE_F_2(__VA_ARGS__)
#define CASE_F_4(X, ...) case X: CASE_F_3(__VA_ARGS__)
#define CASE_F_5(X, ...) case X: CASE_F_4(__VA_ARGS__)
#define CASE_F_6(X, ...) case X: CASE_F_5(__VA_ARGS__)
#define CASE_F_7(X, ...) case X: CASE_F_6(__VA_ARGS__)
#define CASE_F_8(X, ...) case X: CASE_F_7(__VA_ARGS__)
#define CASE_F_9(X, ...) case X: CASE_F_8(__VA_ARGS__)
#define CASE_F_10(X, ...) case X: CASE_F_9(__VA_ARGS__)
#define CASE_F_11(X, ...) case X: CASE_F_10(__VA_ARGS__)
#define CASE_F_12(X, ...) case X: CASE_F_11(__VA_ARGS__)
#define CASE_F_13(X, ...) case X: CASE_F_12(__VA_ARGS__)
#define CASE_F_14(X, ...) case X: CASE_F_13(__VA_ARGS__)
#define CASE_F_15(X, ...) case X: CASE_F_14(__VA_ARGS__)
#define CASE_F_16(X, ...) case X: CASE_F_15(__VA_ARGS__)
#define CASE_F_17(X, ...) case X: CASE_F_16(__VA_ARGS__)
#define CASE_F_18(X, ...) case X: CASE_F_17(__VA_ARGS__)
#define CASE_F_19(X, ...) case X: CASE_F_18(__VA_ARGS__)
#define CASE_F_20(X, ...) case X: CASE_F_19(__VA_ARGS__)
#define CASE_F_21(X, ...) case X: CASE_F_20(__VA_ARGS__)
#define CASE_F_22(X, ...) case X: CASE_F_21(__VA_ARGS__)
/* clang-format on */

#define GET_CASE_F(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, \
                   _20, _21, _22, NAME, ...)                                                             \
  NAME

#define FOR_EACH_MAKE_CASE(...)                                                                        \
  GET_CASE_F(__VA_ARGS__, CASE_F_22, CASE_F_21, CASE_F_20, CASE_F_19, CASE_F_18, CASE_F_17, CASE_F_16, \
             CASE_F_15, CASE_F_14, CASE_F_13, CASE_F_12, CASE_F_11, CASE_F_10, CASE_F_9, CASE_F_8,     \
             CASE_F_7, CASE_F_6, CASE_F_5, CASE_F_4, CASE_F_3, CASE_F_2, CASE_F_1)                     \
  (__VA_ARGS__)

/**
 * @def IN_SET
 * Instead of writing `if (x || y, || ...) ...`
 * Replace with if(IN_SET (x, y, ...))
 */
#define IN_SET(x, first, ...)                                                                       \
  ({                                                                                                \
    bool _found = false;                                                                            \
    /* If the build breaks in the line below, you need to extend the case macros. We use typeof(+x) \
     * here to widen the type of x if it is a bit-field as this would otherwise be illegal. */      \
    static const typeof(+x) __assert_in_set[] AK_UNUSED = {first, __VA_ARGS__};                     \
    static_assert(AK_ARRAY_SIZE(__assert_in_set) <= 22);                                            \
    switch (x) {                                                                                    \
      FOR_EACH_MAKE_CASE(first, __VA_ARGS__)                                                        \
      _found = true;                                                                                \
      break;                                                                                        \
      default:;                                                                                     \
    }                                                                                               \
    _found;                                                                                         \
  })
/** @} */  // Conditionals

/**
 * @def STRLEN
 * Length of a static string (minus the null terminator).
 */
#define STRLEN(x) (sizeof("" x "") - sizeof(typeof((x)[0])))

#define SWAP_TWO(x, y)  \
  do {                  \
    typeof(x) _t = (x); \
    (x) = (y);          \
    (y) = (_t);         \
  } while (false)

/**
 * @name Time conversion utilities
 * Useful constants and inline functions to convert between different time bases.
 * @{
 */

/**
 * One second (s) in microseconds (us).
 * This is useful as FFmpeg uses microseconds for their internal timebase.
 */
#define AK_TIME_SEC_MICROS 1000000ULL

/**
 * One second (s) in microseconds (us).
 * This is useful as FFmpeg uses microseconds for their internal timebase.
 */
#define AK_TIME_SEC_MICROS_FLOAT 1000000.0

/**
 * Converts microseconds to seconds.
 * @param microseconds The value in us.
 * @return The equivalent value in seconds.
 */
static AK_ALWAYS_INLINE AK_CONST double ak_time_microsec_sec (int64_t microseconds) {
  return (double) microseconds / AK_TIME_SEC_MICROS_FLOAT;
}

/**
 * Converts seconds to microseconds.
 * @param seconds The value in decimal seconds.
 * @return The equivalent value in microseconds.
 */
static AK_ALWAYS_INLINE AK_CONST int64_t ak_time_sec_microsec (double seconds) {
  return (seconds <= 0.0) ? 0 : llrint(seconds * AK_TIME_SEC_MICROS_FLOAT);
}

/** @} */  // END TIME

/** @name Math Related Macros
 * Macros to help with numbers and math.
 * @{
 */

/** PI as a float constant */
#define AK_PI_F 3.14159265358979323846F

/** Return larger value from X and Y. */
#define MAXIMUM(X, Y) ((X) > (Y) ? (X) : (Y))

/** Return largest number between X, Y, and Z*/
#define MAXIMUM_3(X, Y, Z) ((X) > (Y) ? ((X) > (Z) ? (X) : (Z)) : ((Y) > (Z) ? (Y) : (Z)))

/** Return smallest number between X and Y */
#define MINIMUM(X, Y) ((X) < (Y) ? (X) : (Y))

/** Return smallest number between X, Y, and Z*/
#define MININUM_3(X, Y, Z) ((X) < (Y) ? ((X) < (Z) ? (X) : (Z)) : ((Y) < (Z) ? (Y) : (Z)))

/** Absolute value of X */
#define ABSOLUTE(X) ((X) > 0 ? (X) : -(X))

/** Difference of X and Y. */
#define DIFF(X, Y) ((X) > (Y) ? (X) - (Y) : (Y) - (X))

/**
 * Range constraint macro to ensure value is between min and max.
 * @param _val The value to clamp
 * @param _max Maximum value to clamp to
 * @param _min Mininmum value to clamp to
 * @return Clamped value
 */
#define CLAMP_BETWEEN(_val, _min, _max) MAXIMUM(MINIMUM((_val), (_max)), (_min))

/** Round up 32 bit integer variable to next power of 2. */
#define ROUNDUP_32(X) \
  (--(X), (X) |= (X) >> 1, (X) |= (X) >> 2, (X) |= (X) >> 4, (X) |= (X) >> 8, (X) |= (X) >> 16, ++(X))

/** Round up 64 bit integer variable to next power of 2. */
#define ROUNDUP_64(X)                                                                           \
  (--(X), (X) |= (X) >> 1, (X) |= (X) >> 2, (X) |= (X) >> 4, (X) |= (X) >> 8, (X) |= (X) >> 16, \
   (X) |= (X) >> 32, ++(X))

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

static_assert(sizeof(unsigned long long) >= 8,
              "Unsigned long longs must be at least 64 bits for our hamming distance "
              "implementation to work.");

/**
 * Calculate hamming distance between two **unsigned** 64-bit integers.
 * Makes use of `__builtin_popcountll() (if available).`.
 * @return Number of bits that differ between `X` and `Y` as an integer.
 * @retval 0 `X` and `Y` are the exact same.
 * @retval 64 `X` and `Y` are compliments of one another.
 * @retval k `X` and `Y` differ by `k` number of bits.
 */
static AK_ALWAYS_INLINE AK_CONST unsigned int hamming_distance (const uint64_t a, const uint64_t b) {
  uint64_t x = a ^ b;

  /* Use popcountll if builtin */
#if __has_builtin(__builtin_popcountll) || (defined(__GNUC__) && __GNUC__ >= 4)
  return (unsigned) __builtin_popcountll(x);

#else
/* SWAR method is quickest to compute hamming distance if no hardware builtins available */
#  pragma message "Using SWAR to compute hamming distance as __builtin_popcountll not available."
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return (unsigned) ((x * 0x0101010101010101ULL) >> 56);
#endif
}

void ak_matrix_fprint_float(FILE *fp, const float *matrix, int rows, int cols);

void ak_io_fprint_indent(FILE *fp, int spaces, int depth);

/**
 * Convert ascii character to lower case
 */
static AK_ALWAYS_INLINE AK_CONST int ak_char_lower (int c) {
  return (('A' <= c) && (c <= 'Z')) ? (c + ('a' - 'A')) : c;
}

/**
 * @def AK_PANIC
 * Print panic message and abort the program as our code is broken.
 * @note To be used only when there is some logical issue in our code.
 */
#define AK_PANIC(message)                                                          \
  do {                                                                             \
    (void) fprintf(stderr, "[PANIC]: %s:%d: %s\n", __FILE__, __LINE__, (message)); \
    abort();                                                                       \
  } while (0)

/**
 * @def AK_DIE
 * Print message and exit as we have encountered external error.
 * @note Used when we encounter issues such as memory allocation failure.
 */
#define AK_DIE(message)                                                            \
  do {                                                                             \
    (void) fprintf(stderr, "[FATAL]: %s:%d: %s\n", __FILE__, __LINE__, (message)); \
    (void) fflush(stderr);                                                         \
    abort();                                                                       \
  } while (0)

#define AK_HANDLE_OOM(x) \
  do {                   \
    void *oom_p_ = (x);  \
    if (!oom_p_)         \
      abort();           \
  } while (0)

/**
 * @def AK_TODO
 * Print message and exit, as this section of code is not implemented yet.
 */
#define AK_TODO(message)                                                        \
  do {                                                                          \
    (void) fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, (message)); \
    (void) fflush(stderr);                                                      \
    abort();                                                                    \
  } while (0)

#ifdef AK_DEBUG  // If its in DEBUG MODE
/* Debug builds should crash when reaching unreachable code. */
#  define AK_UNREACHABLE(message)                                                                      \
    do {                                                                                               \
      (void) fprintf(stderr, "[PANIC] AK_UNREACHABLE CODE REACHED AT %s:%d: %s\n", __FILE__, __LINE__, \
                     (message));                                                                       \
      abort();                                                                                         \
    } while (0)

/* Assumption crashes when false. */
#  define AK_ASSUME(cond)                                                \
    do {                                                                 \
      if (!(cond)) {                                                     \
        (void) fprintf(stderr, "[PANIC] Assertion %s failed at %s:%d\n", \
                       AK_STRINGIFY(cond), __FILE__, __LINE__);          \
        abort();                                                         \
      }                                                                  \
    } while (0)

/* ------------------------------------------------------------------------ */
#else
/* Optimise AK_unreachable code away when in release builds. */
#  define AK_UNREACHABLE(message) __builtin_unreachable()

/* Tell compiler our assumptions are TRUE and optimise out anything contrary. */
#  define AK_ASSUME(cond) \
    do {                  \
      if (!(cond)) {      \
        AK_UNREACHABLE(); \
      }                   \
    } while (0)

#endif  // AK_UNREACHABLE

#endif  // AK_UTIL_H
