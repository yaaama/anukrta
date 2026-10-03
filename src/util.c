#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static AK_NEVER_INLINE AK_COLD_FUNC AK_PRINTF(4, 0) AK_MAYBE_UNUSED AK_NO_RETURN void
ak_vfatal (const char *tag, const char *file, int line, const char *fmt, va_list ap) {
  fprintf(stderr, "anukrta: [%s]: %s:%d: ", tag, file, line);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr); /* abort() doesn't guarantee flushing */
  abort();
}

void ak_fatal_at (char *tag, const char *file, int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  ak_vfatal(tag, file, line, fmt, ap);
  va_end(ap); /* unreachable */
}

/* Helper to visualise matrix */
void ak_matrix_fprint_float (FILE *fp, const float *matrix, const int rows, const int cols) {
  fprintf(fp, "--- %dx%d Visual Dump ---\n", cols, rows);
  for (int y = 0; y < rows; y += 2) {  // Skip every other row to fit screen
    for (int x = 0; x < cols; x++) {
      float val = matrix[(y * cols) + x];
      /* Simple ASCII mapping */
      char c = ' ';
      if (val > 200) {
        c = '#';
      } else if (150 < val) {
        c = '+';
      } else if (100 < val) {
        c = ':';
      } else if (50 < val) {
        c = '.';
      }
      fputc(c, fp);
    }
    fputc('\n', fp);
  }
  fprintf(fp, "-------------------------\n");
}

void ak_io_fprint_indent (FILE *fp, const int spaces, const int depth) {

  if ((depth < 0) || (spaces <= 0)) {
    return;
  }
  FILE *file = fp ? fp : stdout;
  fprintf(file, "%*s", (depth * spaces), "");
}
