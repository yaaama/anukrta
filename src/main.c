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
#include "tree.h"
#include "util.h"
#include "vendor/log.h"
#include "video.h"

typedef struct anukrta_config {
  int segments;
  int threshold;
} anukrta_config;

/* This is the function called ONLY when a valid frame is fully decoded */
static uint64_t hash_decoded_frame (video_io *vreader,
                                    anu_hash_type hash_algo) {

  uint64_t hash = 0;
  AVFrame *grey_frame = NULL;

  log_trace("Trying to alloc grey-frame...");
  grey_frame = av_frame_alloc();
  if (!grey_frame) {
    log_fatal("Failed to allocate memory for grey-frame.");
    exit(EXIT_FAILURE);
  }
  log_trace("Allocated grey-frame.");

  /* Create an empty grey frame */
  if (anu_video_frame_init(ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                           grey_frame)) {
    log_fatal("Failed to initialise frame.");
    exit(EXIT_FAILURE);
  }
  log_trace("Video frame initialised with %dx%d dimensions.",
            ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE);

  /* Scale frame down to 32x32 and store in empty grey frame */
  if (anu_video_scale_frame(vreader->frame, ANU_PHASH_INPUT_SIZE,
                            ANU_PHASH_INPUT_SIZE, grey_frame) != 0) {
    log_fatal("Failed to scale frame!");
    /* Clean up before exiting */
    av_frame_free(&grey_frame);
    exit(EXIT_FAILURE);
  }

  /* Prep a 2D matrix to store greyscale values */
  const size_t matrix_size =
      (size_t)ANU_PHASH_INPUT_SIZE * ANU_PHASH_INPUT_SIZE;
  uint8_t matrix[ANU_PHASH_INPUT_SIZE * ANU_PHASH_INPUT_SIZE];

  /* Populate matrix with frame data */
  copy_frame_to_buffer(grey_frame, matrix, ANU_PHASH_INPUT_SIZE);

  switch (hash_algo) {
    case ANU_HASH_ALGO_DCT:
      {
        hash = dct_hash(matrix);
        break;
      }
    default:
      {
        log_warn("Hashing algorithm not specified.");
      }
  }

  if (hash == 0) {
    log_warn("Received a 0 value for hash.");
  }

  av_frame_free(&grey_frame);
  return hash;
}

int hash_video (anu_file *file, anu_hash_type hash_algo, int segments,
                uint64_t *hashes_out) {

  if (segments <= 0) {
    log_trace("Skipping hash for `%s`\n", file->path);
    return -2;
  }
  video_io vreader;

  /* Setup video reader */
  if (anu_video_open(file->path, &vreader) < 0) {
    /* Cleanup partial opens */
    anu_video_close(&vreader);
    return -1;
  }

  /* Container: vreader.fmt_ctx; */

  /* Video stream */
  AVStream *vid_stream_ptr = anu_video_get_vid_stream(&vreader);

  /* We want to split the video into this many segments */
  int total_video_segments = segments;

  long video_duration_us = vreader.video_duration;
  assert(video_duration_us > 0);

  if (!file->duration_us) {
    file->duration_us = video_duration_us;
  }

#if 1
  if (file->duration_us < (4L * ANU_TIME_ONE_SEC_IN_US)) {
    log_info("Skipping file because duration is less than 4 seconds (%.2f)\n",
             ANU_US_TO_SECONDS((double)file->duration_us));

    anu_video_close(&vreader);
    return -2;
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

    log_debug("Segment [%d/%d] : Seeking to PTS %" PRId64 " (%.1f sec)", i + 1,
              total_video_segments, seek_target_sb,
              (double)seek_target_us / ANU_TIME_ONE_SEC_IN_US);

    /* Seek to timestamp */
    if (anu_video_seek_to_timestamp_pts(&vreader, seek_target_sb) < 0) {
      log_warn("Could not seek to segment `%d`", i);
      continue; /* Try next segment */
    }

    /* Decode packets til we get a frame */
    while (av_read_frame(vreader.fmt_ctx, vreader.packet) >= 0) {

      /* Only process video packets */
      if (vreader.packet->stream_index != vreader.video_stream_idx) {
        av_packet_unref(vreader.packet);
        continue;
      }

      decoding_success = anu_video_decode_packet(&vreader);

      /* Successfully decoded a frame */
      if (decoding_success == 1) {

        current_pts = vreader.frame->best_effort_timestamp;
        if (current_pts < seek_target_sb) {
          av_packet_unref(vreader.packet);
          continue; /* Loop again to get next frame */
        }

        hashes_out[frames_decoded] = hash_decoded_frame(&vreader, hash_algo);

        log_debug("Frame: %ld | Hash: [0x%lX]", vreader.codec_ctx->frame_num,
                  hashes_out[frames_decoded]);

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
        log_warn("Decoding failed.");
        av_packet_unref(vreader.packet);
        break;
      }
      av_packet_unref(vreader.packet);
    }

    if (!frame_found_for_segment) {
      log_warn("No frame decoded for segment `%d`", i);
    }
  }

  /* Cleanup */
  anu_video_close(&vreader);

#ifndef NDEBUG
  char hashes[1024];
  int total_len = 0;
  for (int i = 0; i < frames_decoded; i++) {
    int end = sprintf(&hashes[total_len], "#%d[%lX], ", i, hashes_out[i]);
    total_len += end;
    hashes[total_len] = ' ';
  }
  hashes[total_len] = '\0';
  log_trace("DONE. Processed %d frames.", frames_decoded);
  log_debug("Hashes (%s):\n%s\n", anu_file_get_filename(file), hashes);
#else
  log_trace("DONE. Processed %d frames for %s", frames_decoded, file->path);
#endif

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
    log_fatal("Memory allocation failed.");
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
    log_warn("Encountered an error searching for files.");
    return -1;
  }
  size_t file_count = files.count;

  if (file_count < 1) {
    log_warn("Detected no video files.");
    return -1;
  }

  log_info("Found `%zu` files.", file_count);

  /* Array of hashes */
  size_t hash_collection_len = (file_count * config.segments);
  uint64_t *hashes = calloc(hash_collection_len, sizeof(uint64_t));

  if (!hashes) {
    exit(EXIT_FAILURE);
  }

  anu_file *file;
  bk_tree filetree;

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);

    int hashing_ret = hash_video(file, ANU_HASH_ALGO_DCT, config.segments,
                                 &hashes[i * config.segments]);

    if (hashing_ret == -2) {
      /* We skipped this hash so lets move onto the next file. */
      continue;
    }
    if (hashing_ret == -1) {
      /* Some failure occured. */
      log_error("Failed to hash file %s", anu_file_get_filename(file));
      continue;
    }

    for (int j = 0; j < config.segments; j++) {
      bk_tree_insert(&filetree, hashes[(i * config.segments) + j], i);
    }
  }

  anu_report_duplicates(&files, hashes, &config);
  bk_tree_print_ascii(&filetree);
  bk_tree_node_free(filetree.root);
  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}

int main (int argc, char *argv[]) {  // NOLINT (unused-*)
  char *path = "./etc/";

  log_set_level(LOG_DEBUG);
  anukrta_config config = {.segments = 2, .threshold = 20};
  log_info("%s now running...", argv[0]);
  anukrta_driver(config, path);

  return 0;
}
