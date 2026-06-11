#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/redirect.h>
#include <signal.h>

#include "../src/util.h"

static void setup (void) {
  /* Redirect standard output and err from tests */
  cr_redirect_stdout();
  cr_redirect_stderr();
}

TestSuite(util, .init = setup, .description = "Utility Related Unit Tests");

// --- Math and Comparison Tests ---

Test (util, math_maximum) {
  cr_assert(eq(int, MAXIMUM(10, 20), 20));
  cr_assert(eq(int, MAXIMUM(-5, -10), -5));
  // Use epsilon_eq for floating point to avoid precision issues
  cr_assert(epsilon_eq(dbl, MAXIMUM(15.5, 15.4), 15.5, 0.001));
}

Test (util, math_minimum) {
  cr_assert(eq(int, MINIMUM(10, 20), 10));
  cr_assert(eq(int, MINIMUM(-5, -10), -10));
}

Test (util, math_clamp) {
  cr_assert(eq(int, CLAMP_BETWEEN(50, 0, 100), 50));
  cr_assert(eq(int, CLAMP_BETWEEN(-10, 0, 100), 0));
  cr_assert(eq(int, CLAMP_BETWEEN(150, 0, 100), 100));
}

// --- Rounding Tests ---

Test (util, bits_roundup_32) {
  u32 a = 5;
  ROUNDUP_32(a);
  cr_assert(eq(u32, a, 8), "5 should round up to 8, but got %u", a);

  u32 b = 16;
  ROUNDUP_32(b);
  cr_assert(eq(u32, b, 16));

  u32 c = 0;
  ROUNDUP_32(c);
  cr_assert(eq(u32, c, 0));
}

Test (util, bits_roundup_64) {
  u64 a = 0x100000001ULL;
  ROUNDUP_64(a);
  cr_assert(eq(u64, a, 0x200000000ULL));
}

// --- Array Size Tests ---

Test (util, array_size) {
  int arr_int[15];
  double arr_dbl[42];
  cr_assert(eq(sz, ANU_ARRAY_SIZE(arr_int), 15));
  cr_assert(eq(sz, ANU_ARRAY_SIZE(arr_dbl), 42));
}

// --- Time Conversion Tests ---

Test (util, time_conversions) {
  double sec = 1.5;
  size_t us = anu_time_seconds_to_microseconds(sec);
  cr_assert(eq(sz, us, 1500000));

  size_t input_us = 2000000;
  double output_sec = anu_time_microseconds_to_seconds(input_us);
  cr_assert(epsilon_eq(dbl, output_sec, 2.0, 0.00001));
}

// --- Hamming Distance Tests ---

Test (util, hamming) {
  uint64_t v1 = 0b1010;
  uint64_t v2 = 0b1111;
  cr_assert(eq(int, hamming_distance(v1, v2), 2));
  cr_assert(eq(int, hamming_distance(0xFFFFFFFFFFFFFFFFULL, 0), 64));
}

// --- File Size Constants ---

Test (util, constants) {
  cr_assert(eq(u64, KILOBYTE(1), 1000));
  cr_assert(eq(u64, KIBIBYTE(1), 1024));
  cr_assert(eq(u64, MEGABYTE(1), 1000000ULL));
  cr_assert(eq(u64, MEBIBYTE(1), 1024ULL * 1024ULL));
}

// --- Stringification and Glue ---

Test (util, pp_stringify) { cr_assert(eq(str, STRINGIFY(123), "123")); }

Test (util, pp_glue) {
  int test_val = 50;
  cr_assert(eq(int, JOIN(test, _val), 50));
}

// --- Failure/Death Tests ---
/**
 * Note: In the new syntax, we use cr_assert(death( ... ))
 * instead of the metadata .signal = SIGABRT.
 * This is generally preferred for testing specific lines.
 */

Test (util, death_panic, .signal = SIGABRT) {
  ANU_PANIC("This is an expected panic");
}

Test (util, death_die, .signal = SIGABRT) {
  ANU_DIE("This is an expected fatal exit");
}

Test (util, death_todo, .signal = SIGABRT) {
  ANU_TODO("This feature isn't ready");
}

#ifdef ANU_DEBUG
Test (util, death_assume_fail, .signal = SIGABRT) { ANU_ASSUME(1 == 2); }
#endif
