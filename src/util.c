#include "util.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Helper to visualise matrix */
void print_matrix_float (FILE *fp, const float *matrix, const int rows, const int cols) {
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

void anu_util_print_indent (FILE *fp, const int spaces, const int depth) {

  if ((depth < 0) || (spaces <= 0)) {
    return;
  }
  FILE *file = fp ? fp : stdout;
  fprintf(file, "%*s", (depth * spaces), "");
}
