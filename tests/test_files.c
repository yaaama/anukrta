#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <criterion/new/assert.h>
#include <string.h>

#include "../src/explore.h"

TestSuite(FileUtilities,
          .description = "Test suite for file utility functions.");

Test (FileUtilities, basename) {

  char buffer[100] = {0};
  char *regular = "/etc/random/filename.mp4";
  anu_file_basename_stem(regular, &buffer[0], 100);
  cr_log_info("%s Basename -> %s\n", regular, buffer);
  cr_assert(eq(str, buffer, "filename"));

  memset(&buffer[0], 0, 100);

  char *no_ext = "/etc/random/noextension";
  anu_file_basename_stem(no_ext, &buffer[0], 100);
  cr_log_info("%s Basename -> %s\n", no_ext, buffer);
  cr_assert(eq(str, buffer, "noextension"));

  memset(&buffer[0], 0, 100);
  char *no_slash = "filename.mp4";
  anu_file_basename_stem(no_slash, &buffer[0], 100);
  cr_log_info("%s Basename -> %s\n", no_slash, buffer);
  cr_assert(eq(str, buffer, "filename"));

  memset(&buffer[0], 0, 100);
  char *directory = "/etc/directory/directory2/";
  anu_file_basename_stem(directory, &buffer[0], 100);
  cr_log_info("%s Basename -> %s\n", directory, buffer);
  cr_assert(zero(str, buffer));
}

Test(FileUtilities, ext_supported) {
  /* Valid cases */
  cr_assert_eq(anu_file_ext_supported("video.mp4"), 1);
  cr_assert_eq(anu_file_ext_supported("movie.MKV"), 1); /* Should be case-insensitive */
  cr_assert_eq(anu_file_ext_supported("/path/to/vid.webm"), 1);

  /* Invalid cases */
  cr_assert_eq(anu_file_ext_supported("document.txt"), 0);
  cr_assert_eq(anu_file_ext_supported("no_extension_file"), 0);
  cr_assert_eq(anu_file_ext_supported(".hidden_file"), 0);
  cr_assert_eq(anu_file_ext_supported("music.mp3"), 0); /* Not in video array */
}

Test(FileUtilities, file_queue_operations) {
  anu_file_q q;
  anu_fileq_init(&q, 2);
  cr_log_info("Initialised file queue\n");

  /* IMPORTANT: anu_fileq_destroy calls free(path), so we must malloc them */
  anu_file f1 = {.path = strdup("file1.mp4"), .size = 100};
  anu_file f2 = {.path = strdup("file2.mp4"), .size = 200};
  anu_file f3 = {.path = strdup("file3.mp4"), .size = 300};

  cr_log_info("Added files\n");

  cr_assert_eq(anu_fileq_enqueue(&q, &f1), 0);
  cr_assert_eq(anu_fileq_enqueue(&q, &f2), 0);

  /* This enqueue should trigger a realloc inside anu_fileq_enqueue */
  cr_assert_eq(anu_fileq_enqueue(&q, &f3), 0);

  cr_assert_eq(q.count, 3, "Queue should contain 3 items");
  cr_assert_eq(q.capacity, 4, "Queue capacity should have doubled to 4");

  anu_file out;
  cr_assert_eq(anu_fileq_dequeue(&q, &out), 1);
  cr_assert_eq(out.size, 100, "Dequeued item should be the first one inserted");
  cr_assert_eq(q.count, 2, "Queue count should decrease after dequeue");

  free(out.path);

  cr_assert_eq(anu_fileq_dequeue(&q, &out), 1);
  cr_assert_eq(out.size, 200, "Dequeued item should be the first one inserted");
  cr_assert_eq(q.count, 1, "Queue count should decrease after dequeue");

  free(out.path);

  cr_assert_eq(anu_fileq_dequeue(&q, &out), 1);
  cr_assert_eq(out.size, 300, "Dequeued item should be the first one inserted");
  cr_assert_eq(q.count, 0, "Queue count should decrease after dequeue");

  free(out.path);

  anu_fileq_destroy(&q);
}
