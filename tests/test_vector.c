#include <criterion/criterion.h>
#include <stdint.h>

#include "../src/stack.h"

/* A custom struct to test that the vector handles generic memory correctly */
typedef struct {
  int id;
  float weight;
  char code;
} test_item;

/*
 * Basic Initialization and Cleanup
 */
Test (vector, init_and_destroy) {
  anu_vector v;

  /* Init with capacity 0 */
  anu_vector_init(&v, 0, sizeof(int));

  cr_assert_not_null(v.items, "Items pointer should not be NULL after init");
  cr_assert_eq(v.count, 0, "Initial count should be 0");
  cr_assert_geq(v.capacity, 4, "Capacity should default to at least 4");
  cr_assert_eq(anu_vector_is_empty(&v), 1, "Vector should report as empty");

  anu_vector_destroy(&v);
  cr_assert_null(
      v.items,
      "Items pointer should be NULL after destroy (if you set it to NULL)");
  cr_assert_eq(v.capacity, 0, "Capacity should be 0 after destroy");
}

/*
 * storing Primitives (int)
 * Checks append, count, and get.
 */
Test (vector, int_operations) {
  anu_vector v;
  anu_vector_init(&v, 4, sizeof(int));

  int nums[] = {10, 20, 30};

  anu_vector_append(&v, &nums[0]);
  anu_vector_append(&v, &nums[1]);
  anu_vector_append(&v, &nums[2]);

  cr_assert_eq(anu_vector_count(&v), 3, "Count should be 3");

  int val;

  /* Check Index 0 */
  anu_vector_get(&v, 0, &val);
  cr_assert_eq(val, 10, "Index 0 should be 10");

  /* Check Index 2 */
  anu_vector_get(&v, 2, &val);
  cr_assert_eq(val, 30, "Index 2 should be 30");

  anu_vector_destroy(&v);
}

/*
 * Storing structures
 */
Test (vector, struct_operations) {
  anu_vector v;
  anu_vector_init(&v, 2, sizeof(test_item));

  test_item item1 = {.id = 1, .weight = 12.5F, .code = 'A'};
  test_item item2 = {.id = 99, .weight = 0.5F, .code = 'Z'};

  anu_vector_append(&v, &item1);
  anu_vector_append(&v, &item2);

  test_item out;
  anu_vector_get(&v, 1, &out);

  cr_assert_eq(out.id, 99);
  cr_assert_float_eq(out.weight, 0.5F, 0.001);
  cr_assert_eq(out.code, 'Z');

  anu_vector_destroy(&v);
}

/*
 * Dynamic Resizing (The Realloc Test)
 */
Test (vector, automatic_resize) {
  anu_vector v;
  /* Start with small capacity */
  anu_vector_init(&v, 2, sizeof(int));

  int limit = 100;
  for (int i = 0; i < limit; i++) {
    anu_vector_append(&v, &i);
  }

  cr_assert_eq(anu_vector_count(&v), limit);
  /* Capacity should have kept doubling all the way to 128 */
  cr_assert_eq(v.capacity, 128);

  /* Verify data integrity didn't break during realloc */
  int first;
  int last;
  int mid;
  anu_vector_get(&v, 0, &first);
  anu_vector_get(&v, 50, &mid);
  anu_vector_get(&v, 99, &last);

  cr_assert_eq(first, 0);
  cr_assert_eq(mid, 50);
  cr_assert_eq(last, 99);

  anu_vector_destroy(&v);
}

/*
 * Popping items
 * Tests stack-like behavior and the 'out' parameter.
 */
Test (vector, pop_operations) {
  anu_vector v;
  anu_vector_init(&v, 5, sizeof(int));

  int val = 100;
  /* count 1 */
  anu_vector_append(&v, &val);
  val = 200;
  /* count 2 */
  anu_vector_append(&v, &val);

  int popped;

  /* Pop 200 */
  anu_vector_pop_end(&v, &popped);
  cr_assert_eq(popped, 200, "Should have popped last item (200)");
  cr_assert_eq(anu_vector_count(&v), 1, "Count should decrease to 1");

  /* Pop 100 */
  anu_vector_pop_end(&v, NULL);
  cr_assert_eq(anu_vector_count(&v), 0, "Count should be 0");

  /* Pop empty */
  int res = anu_vector_pop_end(&v, &popped);
  cr_assert_eq(res, 0, "Popping empty vector should return 0/fail code");

  anu_vector_destroy(&v);
}

/*
 * Bounds Checking (Edge Cases)
 */
Test (vector, out_of_bounds) {
  anu_vector v;
  anu_vector_init(&v, 4, sizeof(int));

  int x = 5;
  /* index 0 exists */
  anu_vector_append(&v, &x);

  int out;
  int result;

  /* Try to get index 1 (doesn't exist) */
  result = anu_vector_get(&v, 1, &out);

  cr_assert_eq(result, -1, "Should indicate failure for out of bounds access");

  anu_vector_destroy(&v);
}
