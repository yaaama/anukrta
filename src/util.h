/**
 * @file util.h
 * @brief Utility functions/macros used throughout the codebase.
 */

#ifndef ANU_UTIL_H
#define ANU_UTIL_H

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TOSTRING(s) #s
#define STRINGIFY(s) TOSTRING(s)

#define GLUE(a, b) a##b
#define JOIN(a, b) GLUE(a, b)

#define UNIQ_T(x, uniq) JOIN(__unique_prefix_, JOIN(x, uniq))
#define UNIQ __COUNTER__

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#  error "We require GNUisms to build!"
#endif

/**
 * @name Compilation warning controls
 * Compiler warnings control macros
 * @{
 */
/*
 * Helper macros to allow stringification of pragmas.
 * This prevents macro expansion issues with quotes.
 */
#define PRAGMA_STRINGIFY(a) #a
#define DO_PRAGMA(x) _Pragma(PRAGMA_STRINGIFY(x))

/*
 * Compiler Detection and Push/Pop/Disable Base Macros
 */
#ifdef __clang__
#  define WARNING_PUSH DO_PRAGMA(clang diagnostic push)
#  define WARNING_POP DO_PRAGMA(clang diagnostic pop)
#  define WARNING_DISABLE_CLANG(name) DO_PRAGMA(clang diagnostic ignored name)
#  define WARNING_DISABLE_GCC(name)

#elifdef __GNUC__
#  define WARNING_PUSH DO_PRAGMA(GCC diagnostic push)
#  define WARNING_POP DO_PRAGMA(GCC diagnostic pop)
#  define WARNING_DISABLE_CLANG(name)
#  define WARNING_DISABLE_GCC(name) DO_PRAGMA(GCC diagnostic ignored name)

#else
/* Fallback for unknown compilers to prevent syntax errors */
#  define WARNING_PUSH
#  define WARNING_POP
#  define WARNING_DISABLE_CLANG(name)
#  define WARNING_DISABLE_GCC(name)
#endif

/* Shared Warning Disablers
 * (GCC and Clang use identical flag names for almost all standard warnings) */
#define WARNING_DISABLE_SHARED(name) \
  WARNING_DISABLE_CLANG(name)        \
  WARNING_DISABLE_GCC(name)

/* Unused variables, parameters, or functions */
#define DISABLE_WARNING_UNUSED_VARIABLE WARNING_DISABLE_SHARED("-Wunused-variable")
#define DISABLE_WARNING_UNUSED_PARAMETER WARNING_DISABLE_SHARED("-Wunused-parameter")
#define DISABLE_WARNING_UNUSED_FUNCTION WARNING_DISABLE_SHARED("-Wunused-function")
#define DISABLE_WARNING_UNUSED_CONST_VAR WARNING_DISABLE_SHARED("-Wunused-const-variable")

#define DISABLE_WARNING_UNUSED_ALL \
  DISABLE_WARNING_UNUSED_CONST_VAR \
  DISABLE_WARNING_UNUSED_FUNCTION  \
  DISABLE_WARNING_UNUSED_PARAMETER \
  DISABLE_WARNING_UNUSED_VARIABLE

#define DISABLE_WARNING_SIGN_COMPARE WARNING_DISABLE_SHARED("-Wsign-compare")
#define DISABLE_WARNING_SHADOW WARNING_DISABLE_SHARED("-Wshadow")
#define DISABLE_WARNING_CONVERSION WARNING_DISABLE_SHARED("-Wconversion")
#define DISABLE_WARNING_STRICT_ALIASING WARNING_DISABLE_SHARED("-Wstrict-aliasing")
#define DISABLE_WARNING_FALLTHROUGH WARNING_DISABLE_SHARED("-Wimplicit-fallthrough")
#define DISABLE_WARNING_PADDED WARNING_DISABLE_SHARED("-Wpadded")

/** @} */  // END COMPILER WARNING CONTROLS

/**
 * @name Function/Variable Attributes
 * Function and Variable Attributes
 * @{
 */
#if defined(__GNUC__) || defined(__clang__)

/**
 * @def _unused_
 * @brief Suppresses compiler warnings about unused variables, parameters, or functions.
 *
 * Useful when a variable is only used in certain build configurations (e.g., `#ifdef DEBUG`)
 * or for required function signatures where not all parameters are needed.
 *
 * @par Example Usage:
 * @code
 * void event_handler(int event_id, void* _unused_ context) {
 *     printf("Event: %d\n", event_id);
 * }
 * @endcode
 */
#  define _unused_ __attribute__((unused))

/**
 * @def ALWAYS_INLINE
 * @brief Forces the compiler to inline the function, regardless of optimization limits.
 *
 * Bypasses the compiler's normal cost-benefit analysis for inlining. Use sparingly,
 * typically for very small, performance-critical functions.
 *
 * @par Example Usage:
 * @code
 *  static ALWAYS_INLINE int get_fast_multiplier(int base) {
 *     return base << 2;
 * }
 * @endcode
 */
#  ifdef NDEBUG
#    define ALWAYS_INLINE inline __attribute__((always_inline))
#  else
#    define ALWAYS_INLINE inline
#  endif

/**
 * @def NEVER_INLINE
 * @brief Forces the compiler to never inline the function.
 *
 * @par Example Usage:
 * @code
 *  static NEVER_INLINE int oom_err(char *message) {
 *     printf("%s\n", message);
 *     exit(1);
 * }
 * @endcode
 */
#  define NEVER_INLINE __attribute__((noinline))

/**
 * @def _pure_
 * @brief Marks a function as "pure", meaning it has no side effects.
 *
 * The function's return value must depend ONLY on its parameters and/or global
 * variables. It must not modify global state or perform I/O. This allows the
 * compiler to optimize away redundant calls (e.g., in loops).
 *
 * @par Example Usage:
 * @code
 * int string_hash(const char* str) _pure_;
 * @endcode
 */
#  define _pure_ __attribute__((pure))

/**
 * @def _const_
 * @brief Marks a function as "const", a stricter version of pure.
 *
 * The function's return value must depend ONLY on its parameters. It cannot
 * even read global variables or dereference pointers to global memory.
 * Mathematical functions like `abs()` or `square()` are good examples.
 *
 * @par Example Usage:
 * @code
 * int square(int x) _const_;
 * @endcode
 */
#  define _const_ __attribute__((const))

/**
 * @def _warn_unused_
 * @brief Emits a compiler warning if the caller ignores the return value.
 *
 * Highly recommended for functions that allocate memory, return error codes,
 * or acquire locks, where ignoring the result leads to memory leaks or bugs.
 *
 * @par Example Usage:
 * @code
 * int init_hardware_subsystem(void) _warn_unused_;
 * @endcode
 */
#  define _warn_unused_ __attribute__((warn_unused_result))

/**
 * @def _nonnull_all_
 * @brief Specifies that the compiler should warn if ANY pointer argument is NULL.
 *
 * Applies to all pointer arguments in the function signature. Enables
 * aggressive optimizations by assuming pointers are always valid.
 *
 * @par Example Usage:
 * @code
 * void process_data(const char* input, char* output) _nonnull_all_;
 * @endcode
 */
#  define _nonnull_all_ __attribute__((nonnull))

/**
 * @def _nonnull_
 * @brief Specifies that specific pointer arguments must not be NULL.
 *
 * @param ... A comma-separated list of 1-based parameter indices.
 *
 * @par Example Usage (Arguments 1 and 3 cannot be NULL):
 * @code
 * void safe_memcpy(void* dest, size_t len, const void* src) _nonnull_(1, 3);
 * @endcode
 */
#  define _nonnull_(...) __attribute__((nonnull(__VA_ARGS__)))

/**
 * @def _malloc_
 * @brief Tells the compiler that the function returns a newly allocated pointer.
 *
 * Asserts that the returned pointer cannot alias (overlap) with any other
 * valid pointer in the program. This allows the compiler to perform better
 * alias analysis and optimization.
 *
 * @par Example Usage:
 * @code
 * void* custom_allocator(size_t size) _malloc_;
 * @endcode
 */
#  define _malloc_ __attribute__((malloc))

/**
 * @def _ret_nonnull_
 * Tells the compiler that the function will return a non-null value.
 */
#  define _ret_nonnull_ __attribute__((__returns_nonnull__))

/**
 * @def _no_return_
 * @brief Indicates that the function will never return to its caller.
 *
 * Used for functions that terminate the program (e.g., `exit()`), enter an
 * infinite loop, or throw longjmps/exceptions. Suppresses "reached end of
 * non-void function" warnings.
 *
 * @par Example Usage:
 * @code
 * _no_return_ void fatal_panic(const char* reason);
 * @endcode
 */
#  define _no_return_ __attribute__((noreturn))

/**
 * @def _printf_
 * @brief Enables printf-style format string type checking by the compiler.
 *
 * @param x The 1-based index of the format string parameter.
 * @param y The 1-based index of the first variadic argument (`...`).
 *
 * @par Example Usage:
 * @code
 * // Arg 1 is format string, Arg 2 is the first variadic argument
 * void log_message(const char* fmt, ...) _printf_(1, 2);
 * @endcode
 */
#  define _printf_(x, y) __attribute__((format(printf, x, y)))

/**
 * @def _flatten_
 * @brief Forces the compiler to inline every function called WITHIN this function.
 *
 * Useful for performance-critical wrapper functions where you want to eliminate
 * all function call overhead inside the body of this specific function.
 *
 * @par Example Usage:
 * @code
 * void execute_tight_loop(void) _flatten_ {
 *     step_one(); // Will be inlined
 *     step_two(); // Will be inlined
 * }
 * @endcode
 */
#  define _flatten_ __attribute__((flatten))

/**
 * @def HOT_FUNC
 * @brief Marks a function as a "hot spot" (executed very frequently).
 *
 * Instructs the compiler to optimize this function heavily for speed, and informs
 * branch predictors that calls to this function are highly likely to happen.
 *
 * @par Example Usage:
 * @code
 * void process_audio_sample(float sample) HOT_FUNC;
 * @endcode
 */
#  define HOT_FUNC __attribute__((hot))

/**
 * @def COLD_FUNC
 * @brief Marks a function as "cold" (rarely executed).
 *
 * Instructs the compiler to optimize this function for size rather than speed,
 * and to move its code out of the main execution path to improve CPU instruction
 * caching for the hot code. Ideal for error handling.
 *
 * @par Example Usage:
 * @code
 * void handle_out_of_memory(void) COLD_FUNC;
 * @endcode
 */
#  define COLD_FUNC __attribute__((cold))

/**
 * @def _alloc_
 * @brief Informs the compiler of the allocation size based on 1 or 2 arguments.
 *
 * @param ... A single 1-based index (like malloc), or TWO 1-based indices
 *            (like calloc) where the total size is (arg1 * arg2).
 *
 * @par Example Usage:
 * @code
 * _alloc_(1)    void* custom_malloc(size_t size);
 * _alloc_(1, 2) void* custom_calloc(size_t count, size_t size);
 * @endcode
 */
#  define _alloc_(...) __attribute__((alloc_size(__VA_ARGS__)))

#  if defined(__GNUC__) && (__GNUC__ >= 11)

/**
 * @def _deallocator_
 * @brief Associates an allocation function with its specific deallocation function.
 *
 * This extended version of the malloc attribute tells the compiler's static
 * analyzer exactly how the allocated memory should be freed. This allows the
 * compiler to detect memory leaks, use-after-free bugs, and mismatched
 * allocator/deallocator pairs (e.g., allocating with `custom_malloc` but
 * accidentally freeing with the standard `free()`).
 *
 * @param deallocator The name of the function used to free the returned pointer.
 * @param ptr_index   The 1-based index of the argument in the deallocator
 *                    function that receives the pointer to be freed.
 *
 * @par Example Usage:
 * @code
 * // Forward declaration of the deallocator is required first
 * void custom_free(void* ptr);
 *
 * // Tell the compiler that custom_malloc pairs with custom_free,
 * // and the pointer is passed as the 1st argument to custom_free.
 * _deallocator_(custom_free, 1) void* custom_malloc(size_t size);
 *
 * void test_function() {
 *     void* ptr = custom_malloc(128);
 *     free(ptr); // Compiler warning: 'ptr' should have been freed with 'custom_free'
 * }              // Compiler warning if not freed at all: memory leak detected
 * @endcode
 */
#    define _deallocator_(dealloc, idx) __attribute__((malloc(dealloc, idx)))
#  else
#    define _deallocator_(dealloc, idx)
#  endif

#else

#  define ALWAYS_INLINE inline
#  define NEVER_INLINE
#  define HOT_FUNC
#  define COLD_FUNC

#  define _unused_
#  define _flatten_
#  define _unused_
#  define _pure_
#  define _const_
#  define _warn_unused_
#  define _nonnull_all_
#  define _nonnull_(...)
#  define _malloc_
#  define _no_return_
#  define _printf_(x, y)
#  define _flatten_

#endif
/** @} */  // End Function Attributes

#if __has_builtin(__builtin_expect)

/**
 * @def LIKELY
 * @brief Hint to compiler that the branch is most likely *TRUE*.
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
#  define LIKELY(x) __builtin_expect(!!(x), 1)

/**
 * @def UNLIKELY
 * Hint to the compiler the condition is most likely *FALSE*.
 */
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)

#else
#  define LIKELY(x) (x)
#  define UNLIKELY(x) (x)
#endif

/**
 * @name Cleanup Macros (RAII)
 * Helper macros to automatically free/close resources when out of scope.
 * @{
 */

/**
 * @def __cleanup
 * Automatically cleanup allocated types.
 */
#define __cleanup(f) __attribute__((cleanup(f)))

/**
 * @brief Dummy function to ensure we check pointers during the close.
 */
static ALWAYS_INLINE _warn_unused_ void *__ptr_must_check (void *p) { return p; }

/**
 * @def DEFINE_FREE
 * Creates the wrapper function the compiler actually calls the cleanup function.
 * @param _name Name of cleanup function.
 * @param _type Type of object to act on.
 * @param _free Statement to free object.
 */
#define DEFINE_FREE(_name, _type, _free)              \
  static ALWAYS_INLINE void __free_##_name(void *p) { \
    _type _T = *(_type *) p;                          \
    _free;                                            \
  }

/**
 * @def __free
 * The attribute to put on a variable to clean up later.
 */
#define __free(_name) __cleanup(__free_##_name)

/**
 * @def no_free_ptr
 * Prevent automatic cleanup of pointer.
 */
#define no_free_ptr(p)                 \
  ((__typeof__(p)) __ptr_must_check(({ \
    __typeof__(p) __val = (p);         \
    (p) = NULL;                        \
    __val;                             \
  })))

/**
 * @def return_ptr
 * Wrapper around no_free_ptr for return statements.
 */
#define return_ptr(p) return no_free_ptr(p)

/**
 * Free an allocated pointer.
 */
DEFINE_FREE(ptr, void *, if (_T) free(_T))

/**
 * Close a file descriptor.
 */
DEFINE_FREE(fd_close, int, if (_T >= 0) close(_T))

/**
 * @brief Close DIR* type.
 */
DEFINE_FREE(dir_close, DIR *, if (_T) closedir(_T))

/**
 * @brief Close FILE* type.
 */
DEFINE_FREE(f_close, FILE *, if (_T) fclose(_T))

/** @} */  // end cleanup macros

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
 * @def ANU_UPDATED_FLAG
 * @brief Returns a NEW mask with flag(s) conditionally set or cleared based on 'b'.
 * @note Does NOT modify the original mask in place.
 */
#define ANU_UPDATED_FLAG(orig, flag, b) ((b) ? ((orig) | (flag)) : ((orig) & ~(flag)))

/**
 * @def ANU_SET_FLAG_WHEN
 * @brief Conditionally sets or clears a flag in the bitmask in place.
 *
 * ```c
 * // Sets STATUS_RUNNING if 'is_moving' is true, clears it if false.
 * ANU_SET_FLAG_WHEN(player_state, STATUS_RUNNING, is_moving);
 * ```
 */
#define ANU_SET_FLAG_WHEN(mask, flag, b) ((mask) = ANU_UPDATED_FLAG(mask, flag, b))

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
#define ANU_HAS_ALL_FLAGS(mask, flag) ((~(mask) & (flag)) == (0))

/**
 * @def ANU_HAS_ANY_FLAG
 * @brief Checks if ANY of the specified flags are set.
 */
#define ANU_HAS_ANY_FLAG(mask, flag) (((mask) & (flag)) != 0)

/** @} */  // End BitMacros group

/**
 * @name Utility Macros
 * Generally useful macros.
 * @{
 */

#define VOID_0 ((void) 0)

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
#define ANU_ARRAY_SIZE(array) \
  ((sizeof(array) / sizeof((array)[0])) / ((size_t) (!(sizeof(array) % sizeof((array)[0])))))

/**
 * @def ARRAY_LAST_ENTRY
 * @brief Get last array entry.
 *
 * @note This should be called with a real array.
 * @warning Calling this with a pointer is an *error*.
 *
 * Snippet derived from neovim (neovim/src/nvim/macros_defs.h)
 * Licensed under Apache 2.0: https://www.apache.org/licenses/LICENSE-2.0
 */
#define ARRAY_LAST_ENTRY(array) (array)[ANU_ARRAY_SIZE(array) - 1]

/**
 * @def ANU_ZERO_MEMORY
 * @brief Zero out memory. */
#define ZERO_MEMORY(pointer, count, type) memset((pointer), 0, (count) * sizeof(type))

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
#define CASE_F_23(X, ...) case X: CASE_F_22(__VA_ARGS__)
#define CASE_F_24(X, ...) case X: CASE_F_23(__VA_ARGS__)
#define CASE_F_25(X, ...) case X: CASE_F_24(__VA_ARGS__)
#define CASE_F_26(X, ...) case X: CASE_F_25(__VA_ARGS__)
#define CASE_F_27(X, ...) case X: CASE_F_26(__VA_ARGS__)
#define CASE_F_28(X, ...) case X: CASE_F_27(__VA_ARGS__)
#define CASE_F_29(X, ...) case X: CASE_F_28(__VA_ARGS__)
#define CASE_F_30(X, ...) case X: CASE_F_29(__VA_ARGS__)
#define CASE_F_31(X, ...) case X: CASE_F_30(__VA_ARGS__)
#define CASE_F_32(X, ...) case X: CASE_F_31(__VA_ARGS__)
#define CASE_F_33(X, ...) case X: CASE_F_32(__VA_ARGS__)
#define CASE_F_34(X, ...) case X: CASE_F_33(__VA_ARGS__)
#define CASE_F_35(X, ...) case X: CASE_F_34(__VA_ARGS__)
#define CASE_F_36(X, ...) case X: CASE_F_35(__VA_ARGS__)
#define CASE_F_37(X, ...) case X: CASE_F_36(__VA_ARGS__)
#define CASE_F_38(X, ...) case X: CASE_F_37(__VA_ARGS__)
#define CASE_F_39(X, ...) case X: CASE_F_38(__VA_ARGS__)
#define CASE_F_40(X, ...) case X: CASE_F_39(__VA_ARGS__)
#define CASE_F_41(X, ...) case X: CASE_F_40(__VA_ARGS__)
#define CASE_F_42(X, ...) case X: CASE_F_41(__VA_ARGS__)
#define CASE_F_43(X, ...) case X: CASE_F_42(__VA_ARGS__)
#define CASE_F_44(X, ...) case X: CASE_F_43(__VA_ARGS__)
#define CASE_F_45(X, ...) case X: CASE_F_44(__VA_ARGS__)
#define CASE_F_46(X, ...) case X: CASE_F_45(__VA_ARGS__)
#define CASE_F_47(X, ...) case X: CASE_F_46(__VA_ARGS__)
#define CASE_F_48(X, ...) case X: CASE_F_47(__VA_ARGS__)
#define CASE_F_49(X, ...) case X: CASE_F_48(__VA_ARGS__)
#define CASE_F_50(X, ...) case X: CASE_F_49(__VA_ARGS__)
#define CASE_F_51(X, ...) case X: CASE_F_50(__VA_ARGS__)
#define CASE_F_52(X, ...) case X: CASE_F_51(__VA_ARGS__)
#define CASE_F_53(X, ...) case X: CASE_F_52(__VA_ARGS__)
#define CASE_F_54(X, ...) case X: CASE_F_53(__VA_ARGS__)
#define CASE_F_55(X, ...) case X: CASE_F_54(__VA_ARGS__)
#define CASE_F_56(X, ...) case X: CASE_F_55(__VA_ARGS__)
#define CASE_F_57(X, ...) case X: CASE_F_56(__VA_ARGS__)
#define CASE_F_58(X, ...) case X: CASE_F_57(__VA_ARGS__)
#define CASE_F_59(X, ...) case X: CASE_F_58(__VA_ARGS__)
#define CASE_F_60(X, ...) case X: CASE_F_59(__VA_ARGS__)
#define CASE_F_61(X, ...) case X: CASE_F_60(__VA_ARGS__)
#define CASE_F_62(X, ...) case X: CASE_F_61(__VA_ARGS__)
#define CASE_F_63(X, ...) case X: CASE_F_62(__VA_ARGS__)
#define CASE_F_64(X, ...) case X: CASE_F_63(__VA_ARGS__)
/* clang-format on */

#define GET_CASE_F(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, \
                   _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36,  \
                   _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53,  \
                   _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, NAME, ...)                     \
  NAME

#define FOR_EACH_MAKE_CASE(...)                                                                        \
  GET_CASE_F(__VA_ARGS__, CASE_F_64, CASE_F_63, CASE_F_62, CASE_F_61, CASE_F_60, CASE_F_59, CASE_F_58, \
             CASE_F_57, CASE_F_56, CASE_F_55, CASE_F_54, CASE_F_53, CASE_F_52, CASE_F_51, CASE_F_50,   \
             CASE_F_49, CASE_F_48, CASE_F_47, CASE_F_46, CASE_F_45, CASE_F_44, CASE_F_43, CASE_F_42,   \
             CASE_F_41, CASE_F_40, CASE_F_39, CASE_F_38, CASE_F_37, CASE_F_36, CASE_F_35, CASE_F_34,   \
             CASE_F_33, CASE_F_32, CASE_F_31, CASE_F_30, CASE_F_29, CASE_F_28, CASE_F_27, CASE_F_26,   \
             CASE_F_25, CASE_F_24, CASE_F_23, CASE_F_22, CASE_F_21, CASE_F_20, CASE_F_19, CASE_F_18,   \
             CASE_F_17, CASE_F_16, CASE_F_15, CASE_F_14, CASE_F_13, CASE_F_12, CASE_F_11, CASE_F_10,   \
             CASE_F_9, CASE_F_8, CASE_F_7, CASE_F_6, CASE_F_5, CASE_F_4, CASE_F_3, CASE_F_2, CASE_F_1) \
  (__VA_ARGS__)

#define assert_cc(expr) _Static_assert(expr, #expr)

#define ELEMENTSOF(x)                                                             \
  (__builtin_choose_expr(!__builtin_types_compatible_p(typeof(x), typeof(&*(x))), \
                         sizeof(x) / sizeof((x)[0]), VOID_0))

/**
 * Instead of writing if (x || y)...
 * Replace with if(IN_SET (x,y)) */
#define IN_SET(x, first, ...)                                                                       \
  ({                                                                                                \
    bool _found = false;                                                                            \
    /* If the build breaks in the line below, you need to extend the case macros. We use typeof(+x) \
     * here to widen the type of x if it is a bit-field as this would otherwise be illegal. */      \
    static const typeof(+x) __assert_in_set[] _unused_ = {first, __VA_ARGS__};                      \
    assert_cc(ELEMENTSOF(__assert_in_set) <= 64);                                                   \
    switch (x) {                                                                                    \
      FOR_EACH_MAKE_CASE(first, __VA_ARGS__)                                                        \
      _found = true;                                                                                \
      break;                                                                                        \
      default:;                                                                                     \
    }                                                                                               \
    _found;                                                                                         \
  })

// NOLINTBEGIN (bugprone-macro-parentheses)
#define _FOREACH_ARRAY(i, array, num, m, end)   \
  for (typeof(array[0]) *i = (array), *end = ({ \
         typeof(num) m = (num);                 \
         (i && m > 0) ? i + m : NULL;           \
       });                                      \
       end && i < end; i++)

#define FOREACH_ARRAY(i, array, num) _FOREACH_ARRAY(i, array, num, UNIQ_T(m, UNIQ), UNIQ_T(end, UNIQ))

#define FOREACH_ELEMENT(i, array) FOREACH_ARRAY(i, array, ELEMENTSOF(array))

#define _STRV_FOREACH(s, l, i) for (typeof(*(l)) *s, *i = (l); (s = i) && *i; i++)

#define STRV_FOREACH(s, l) _STRV_FOREACH(s, l, UNIQ_T(i, UNIQ))
/* NOLINTEND */

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
 * @brief One second (s) in microseconds (us).
 * This is useful as FFmpeg uses microseconds for their internal timebase.
 */
#define ANU_TIME_ONE_SEC_IN_US_FLOAT 1000000.0

/**
 * @brief Converts microseconds to seconds.
 * @param microseconds The value in us.
 * @return The equivalent value in seconds.
 */
static ALWAYS_INLINE _const_ double anu_time_microseconds_to_seconds (int64_t microseconds) {
  return (double) microseconds / ANU_TIME_ONE_SEC_IN_US_FLOAT;
}

/**
 * @brief Converts seconds to microseconds.
 * @param seconds The value in decimal seconds.
 * @return The equivalent value in microseconds.
 */
static ALWAYS_INLINE _const_ int64_t anu_time_seconds_to_microseconds (double seconds) {
  return (seconds <= 0.0) ? 0 : llrint(seconds * ANU_TIME_ONE_SEC_IN_US_FLOAT);
}

/** @} */  // END TIME

/** @name Math Related Macros
 * Macros to help with numbers and math.
 * @{
 */

/** PI as a float constant */
#define ANU_PI_F 3.14159265358979323846F

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
 * @brief Range constraint macro to ensure value is between min and max.
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
 * @brief Calculate hamming distance between two **unsigned** 64-bit integers.
 * Makes use of `__builtin_popcountll() (if available).`.
 * @return Number of bits that differ between `X` and `Y` as an integer.
 * @retval 0 `X` and `Y` are the exact same.
 * @retval 64 `X` and `Y` are compliments of one another.
 * @retval k `X` and `Y` differ by `k` number of bits.
 */
static ALWAYS_INLINE _const_ unsigned int hamming_distance (const uint64_t a, const uint64_t b) {
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

void print_matrix_float(FILE *fp, const float *matrix, int rows, int cols);

void anu_util_print_indent(FILE *fp, int spaces, int depth);

/**
 * @brief Convert ascii character to lower case
 */
static ALWAYS_INLINE _const_ int anu_util_tolower (int c) {
  return (('A' <= c) && (c <= 'Z')) ? (c + ('a' - 'A')) : c;
}

/**
 * @def ANU_PANIC
 * @brief Print panic message and abort the program as our code is broken.
 * @note To be used only when there is some logical issue in our code.
 */
#define ANU_PANIC(message)                                                         \
  do {                                                                             \
    (void) fprintf(stderr, "[PANIC]: %s:%d: %s\n", __FILE__, __LINE__, (message)); \
    abort();                                                                       \
  } while (0)

/**
 * @def ANU_DIE
 * @brief Print message and exit as we have encountered external error.
 * @note Used when we encounter issues such as memory allocation failure.
 */
#define ANU_DIE(message)                                                           \
  do {                                                                             \
    (void) fprintf(stderr, "[FATAL]: %s:%d: %s\n", __FILE__, __LINE__, (message)); \
    (void) fflush(stderr);                                                         \
    abort();                                                                       \
  } while (0)

#define ANU_HANDLE_OOM(x) \
  do {                    \
    void *oom_p_ = (x);   \
    if (!oom_p_)          \
      abort();            \
  } while (0)

/**
 * @def ANU_TODO
 * @brief Print message and exit, as this section of code is not implemented yet.
 */
#define ANU_TODO(message)                                                       \
  do {                                                                          \
    (void) fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, (message)); \
    (void) fflush(stderr);                                                      \
    abort();                                                                    \
  } while (0)

#ifdef ANU_DEBUG  // If its in DEBUG MODE
/* Debug builds should crash when reaching unreachable code. */
#  define ANU_UNREACHABLE(message)                                                                      \
    do {                                                                                                \
      (void) fprintf(stderr, "[PANIC] ANU_UNREACHABLE CODE REACHED AT %s:%d: %s\n", __FILE__, __LINE__, \
                     (message));                                                                        \
      abort();                                                                                          \
    } while (0)

/* Assumption crashes when false. */
#  define ANU_ASSUME(cond)                                                                          \
    do {                                                                                            \
      if (!(cond)) {                                                                                \
        (void) fprintf(stderr, "[PANIC] Assertion %s failed at %s:%d\n", STRINGIFY(cond), __FILE__, \
                       __LINE__);                                                                   \
        abort();                                                                                    \
      }                                                                                             \
    } while (0)

/* ------------------------------------------------------------------------ */
#else
/* Optimise ANU_unreachable code away when in release builds. */
#  define ANU_UNREACHABLE(message) __builtin_unreachable()

/* Tell compiler our assumptions are TRUE and optimise out anything contrary. */
#  define ANU_ASSUME(cond) \
    do {                   \
      if (!(cond)) {       \
        ANU_UNREACHABLE(); \
      }                    \
    } while (0)

#endif  // ANU_UNREACHABLE

#endif  // ANU_UTIL_H
