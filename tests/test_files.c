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
