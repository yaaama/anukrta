/* Video similarity tool */

#include <assert.h>
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "explore.h"
#include "hash.h"
#include "util.h"
#include "video.h"

typedef struct anukrta_config {
  int segments;
  int threshold;
} anukrta_config;

/* This is the function called ONLY when a valid frame is fully decoded */
static uint64_t hash_decoded_frame (video_io *vreader, anuHashType hash_algo) {

  AVFrame *grey_frame = av_frame_alloc();
  if (grey_frame == NULL) {
    fprintf(stderr, "Failed to allocate memory for frame.\n");
    exit(EXIT_FAILURE);
  }

  /* Create an empty grey frame */
  if (init_grey_frame(ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE, grey_frame)) {
    fprintf(stderr, "Failed to initialise frame.\n");
    exit(EXIT_FAILURE);
  }

  /* Scale frame down to 32x32 and store in empty grey frame */
  if (scale_frame(vreader->frame, ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                  grey_frame)) {
    fprintf(stderr, "Failed to scale frame!");
    /* Clean up before aborting */
    av_frame_free(&grey_frame);
    exit(EXIT_FAILURE);
  }

  /* Generate a 2D matrix of the greyscale values */
  uint8_t matrix[ANU_PHASH_INPUT_SIZE * ANU_PHASH_INPUT_SIZE];

  /* Populate matrix with frame data */
  uint8_t *row_begin = grey_frame->data[0];
  int greyframe_row_len = grey_frame->linesize[0];
  uint8_t *row_ptr;
  uint8_t *dest_row;
  for (long y = 0; y < ANU_PHASH_INPUT_SIZE; y++) {
    row_ptr = &row_begin[y * greyframe_row_len];
    dest_row = &matrix[y * ANU_PHASH_INPUT_SIZE];
    /* We copy row by row using memcpy */
    memcpy(dest_row, row_ptr, ANU_PHASH_INPUT_SIZE);
  }

  uint64_t hash = 0;
  switch (hash_algo) {
    case ANU_HASH_ALGO_DCT:
      {
        hash = dct_hash(matrix);
        break;
      }
    default:
      {
        fprintf(stderr, "Hashing algorithm not specified.");
      }
  }

  if (hash == 0) {
    fprintf(stderr, "Received a 0 value for hash.\n");
  }

  av_frame_free(&grey_frame);
  return hash;
}

int hash_video (anu_file *file, anuHashType hash_algo, int segments,
                uint64_t *hashes_out) {

  if (segments <= 0) {
    printf("Skipping hash for `%s`\n", file->path);
    return 0;
  }
  video_io vreader;

  /* Setup video reader */
  if (open_video_reader(file->path, &vreader) < 0) {
    /* Cleanup partial opens */
    close_video_reader(&vreader);
    return -1;
  }

  /* Container: vreader.fmt_ctx; */

  /* Video stream */
  AVStream *vid_stream_ptr = vreader_get_video_stream(&vreader);

  /* We want to split the video into this many segments */
  int total_video_segments = segments;

  long video_duration_us = get_video_duration(&vreader);
  assert(video_duration_us > 0);

  vreader.video_duration = video_duration_us;

  if (!file->duration_us) {
    file->duration_us = video_duration_us;
  }

#if 0
  if (file->duration_us < (4L * ANU_TIME_ONE_SEC_IN_US)) {
    printf("Skipping file because duration is less than 4 seconds (%.2f)\n",
           ANU_US_TO_SECONDS((double)file->duration_us));
    goto cleanup;
  }
#endif

  long frame_step_us = video_duration_us / total_video_segments;
  /* Counter for # of frames successfully decoded */
  int frames_decoded = 0;
  /* Target timestamp in microseconds */
  long seek_target_us = 0;
  /* Target timestamp in streams timebase (tick) */
  long seek_target_sb = 0;

  /* Return value of `decode_packet` */
  int decoding_success = 0;
  /* Loop will turn this true when we have decoded a frame for the segment */
  bool frame_found_for_segment = false;
  long current_pts = 0;

  for (int i = 0; i < total_video_segments; i++) {
    frame_found_for_segment = false;

    seek_target_us = ((long)i * frame_step_us);
    seek_target_sb =
        av_rescale_q(seek_target_us, AV_TIME_BASE_Q, vid_stream_ptr->time_base);

    printf("--- Segment %d/%d : Seeking to PTS %" PRId64 " (%.2f sec) ---\n",
           i + 1, total_video_segments, seek_target_sb,
           (double)seek_target_us / ANU_TIME_ONE_SEC_IN_US);

    /* Seek to timestamp */
    if (seek_to_timestamp(&vreader, seek_target_sb) < 0) {
      fprintf(stderr, "Could not seek to segment %d\n", i);
      continue; /* Try next segment */
    }

    /* Decode packets til we get a frame */
    while (av_read_frame(vreader.fmt_ctx, vreader.packet) >= 0) {

      /* Only process video packets */
      if (vreader.packet->stream_index != vreader.video_stream_idx) {
        av_packet_unref(vreader.packet);
        continue;
      }

      decoding_success = decode_packet(&vreader);

      /* Successfully decoded a frame */
      if (decoding_success == 1) {

        current_pts = vreader.frame->best_effort_timestamp;
        if (current_pts < seek_target_sb) {
          /* printf("Skipping frame at PTS %ld (Target: %ld)\n", current_pts,
           * target_timestamp); */
          av_packet_unref(vreader.packet);
          continue; /* Loop again to get next frame */
        }

        printf("\tHashing Frame `%ld`\n", vreader.codec_ctx->frame_num);
        hashes_out[frames_decoded] = hash_decoded_frame(&vreader, hash_algo);

        printf("\t%5s", "-----> ");
        printf("Hash: [0x%016" PRIx64, hashes_out[frames_decoded]);
        printf("]\n");

        frame_found_for_segment = true;
        frames_decoded++;
        av_packet_unref(vreader.packet);
        /* Stop reading packets for this segment */
        break;
      }

      /* We need more data... */
      if (decoding_success == 0) {
        av_packet_unref(vreader.packet);
        continue;
      }

      /* Decoding error encountered */
      if (decoding_success < 0) {
        fprintf(stderr, "Decoding failed.\n");
        av_packet_unref(vreader.packet);
        break;
      }
      av_packet_unref(vreader.packet);
    }

    if (!frame_found_for_segment) {
      printf("Warning: No frame decoded for segment %d\n", i);
    }
  }

  /* Cleanup */
  close_video_reader(&vreader);

  printf("Done. Processed %d frames.\n", frames_decoded);
  return 0;
}

size_t anu_report_duplicates (const anu_file_q *files, const uint64_t *hashes,
                              anukrta_config *config) {

  if (files->count == 0) {
    return 0;
  }
  int segments = config->segments;
  int threshold = config->threshold;

  /* array to mark files we've already grouped so we don't process them twice */
  bool *reported = calloc(files->count, sizeof(bool));

  if (!reported) {
    fprintf(stderr, "Memory allocation failed.\n");
    return 0;
  }

  printf("\n\n========================================\n");
  printf("SIMILARITY REPORT (Threshold: <= %d)\n", threshold);
  printf("========================================\n");

  size_t groups_found = 0;
  anu_file *file_a;
  anu_file *file_b;
  uint64_t *hash_a;
  uint64_t *hash_b;
  uint64_t total_dist = 0;
  for (size_t i = 0; i < files->count; i++) {

    if (reported[i]) {
      continue;
    }

    file_a = &files->items[i];
    hash_a = &hashes[i * segments];

    bool header_printed = false;

    /* Inner loop: Compare against all subsequent files */
    for (size_t j = i + 1; j < files->count; j++) {
      if (reported[j]) {
        continue;
      }

      file_b = &files->items[j];
      hash_b = &hashes[j * segments];

      /* Calculate total distance across all segments */
      total_dist = 0;

      for (int seg = 0; seg < segments; seg++) {
        total_dist += hamming_distance((hash_a[seg]), (hash_b[seg]));
      }
      /* printf("Total Distance between %s and %s: %lu\n", file_a->path,
       * file_b->path, total_dist); */

      /* Check against threshold */
      if (total_dist <= (uint64_t)threshold) {

        /* Print Group Header (only once per group) */
        if (!header_printed) {
          groups_found++;
          header_printed = true;
          printf("Group #%zu: %s\n", groups_found,
                 anu_file_get_filename(file_a));
        }

        /* Print the match */
        printf("%s\n", anu_file_get_filename(file_a));
        printf("|--- [Dist: %lu] %s\n", total_dist,
               anu_file_get_filename(file_b));

        /* Mark B as handled so it doesn't start its own group later */
        reported[j] = true;
      }
    }

    if (header_printed) {
      printf("----------------------------------------\n");
    }
  }

  if (groups_found == 0) {
    printf("No similar files found.\n");
  } else {
    printf("Total Groups Found: %zu\n", groups_found);
  }

  free(reported);
  return groups_found;
}

int anukrta_driver (anukrta_config config, char *path) {

  /* Store the files we find in the path */
  anu_file_q files;
  /* Initialise the list to 20 items */
  anu_fileq_init(&files, 20);

  if (anu_recursive_filewalk(path, &files)) {
    fprintf(stderr, "Encountered an error searching for files.");
    return -1;
  }
  size_t file_count = files.count;

  if (file_count < 1) {
    fprintf(stderr, "Detected no video files.\n");
    return -1;
  }
  printf("\nFILE COUNT: `%zu`\n", file_count);

  /* Array of hashes */
  uint64_t *hashes = calloc((file_count * config.segments), sizeof(uint64_t));

  if (!hashes) {
    exit(EXIT_FAILURE);
  }

  anu_file *file;

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);
    hash_video(file, ANU_HASH_ALGO_DCT, config.segments,
               &hashes[i * config.segments]);
  }

  anu_report_duplicates(&files, hashes, &config);
  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}

int main (int argc, char *argv[]) {  // NOLINT (unused-*)

  char *path = "./etc/";
  anukrta_config config = {.segments = 2, .threshold = 20};
  anukrta_driver(config, path);

  return 0;
}
