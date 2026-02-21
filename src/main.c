/* Video similarity tool */

#ifdef ANU_DEBUG
#    pragma message "Compilation in DEBUG mode."
#endif

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
#include "report.h"
#include "tree.h"
#include "util.h"
#include "vendor/log.h"
#include "video.h"

/* This is the function called ONLY when a valid frame is fully decoded */
static uint64_t hash_decoded_frame (video_io *vreader,
                                    anu_hash_type hash_algo) {

  uint64_t hash = 0;
  AVFrame *grey_frame = NULL;
  log_trace("Trying to alloc grey-frame...");
  grey_frame = av_frame_alloc();

  if (!grey_frame) {
    log_fatal("Failed to allocate memory for grey-frame.");
    goto cleanup;
  }
  log_trace("Allocated grey-frame.");

  /* Create an empty grey frame */
  if (anu_video_frame_init(ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                           grey_frame)) {
    log_fatal("Failed to initialise frame.");
    goto cleanup;
  }
  log_trace("Video frame initialised with %dx%d dimensions.",
            ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE);

  /* Scale frame down to 32x32 and store in empty grey frame */
  if (anu_video_scale_frame(vreader->frame, ANU_PHASH_INPUT_SIZE,
                            ANU_PHASH_INPUT_SIZE, grey_frame) != 0) {
    log_fatal("Failed to scale frame!");
    /* Clean up before exiting */
    goto cleanup;
  }

  /* Prep a 2D matrix to store greyscale values */
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

cleanup:
  {

    if (grey_frame) {
      av_frame_free(&grey_frame);
    }
  }

  if (hash == 0) {
    log_warn("Received a 0 value for hash.");
  }

  return hash;
}

static int hash_video (anu_file *file, anukrta_config *config,
                       uint64_t *hashes_out) {

  if (config->segments <= 0) {
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
  int total_video_segments = config->segments;

  long video_duration_us = vreader.video_duration;
  assert(video_duration_us > 0);

  if (!file->duration_us) {
    file->duration_us = video_duration_us;
  }

  if (file->duration_us <= config->skip_duration) {
    log_info("Skipping File - Duration less than threshold (%.1f < %.1f) ",
             anu_time_microseconds_to_seconds(file->duration_us),
             anu_time_microseconds_to_seconds(config->skip_duration));

    anu_video_close(&vreader);
    return -2;
  }

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

    seek_target_us = ((long) i * frame_step_us);
    seek_target_sb =
        av_rescale_q(seek_target_us, AV_TIME_BASE_Q, vid_stream_ptr->time_base);

    log_debug("--- Segment [%d/%d] ---", i + 1, total_video_segments);
    log_debug("Seeking to PTS %d (%.1f sec)", seek_target_sb,
              anu_time_microseconds_to_seconds(seek_target_us));

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

        /* Decoded frame PTS */
        current_pts = vreader.frame->best_effort_timestamp;
        /* If PTS is lower than seek_target, then repeat the loop */
        if (current_pts < seek_target_sb) {
          av_packet_unref(vreader.packet);
          continue; /* Loop again to get next frame */
        }

        hashes_out[frames_decoded] =
            hash_decoded_frame(&vreader, config->hash_algorithm);

        log_debug("Frame '%ld' => %lX", vreader.codec_ctx->frame_num,
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

#ifdef ANU_DEBUG

  char hashes[1024];
  int total_len = 0;
  for (int i = 0; i < frames_decoded; i++) {
    int end = sprintf(&hashes[total_len], " %lX,", hashes_out[i]);
    total_len += end;
    hashes[total_len] = ' ';
  }
  hashes[total_len] = '\0';
  log_debug("Hashed '%s' => {%s}\n", anu_file_get_filename(file), hashes);
#endif
  log_trace("DONE. Processed %d frames for %s", frames_decoded, file->path);
  return 0;
}

int anukrta_driver (anukrta_config *config, char *path) {

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
  size_t hash_collection_len = (file_count * config->segments);
  uint64_t *hashes = calloc(hash_collection_len, sizeof(uint64_t));

  if (!hashes) {
    exit(EXIT_FAILURE);
  }

  anu_file *file;
  bk_tree filetree = {0};

  for (size_t i = 0; i < file_count; i++) {
    file = (files.items + i);

    int hashing_ret = hash_video(file, config, &hashes[i * config->segments]);

    if (hashing_ret == -2) {
      /* We skipped this hash so lets move onto the next file. */
      continue;
    }
    if (hashing_ret == -1) {
      /* Some failure occured. */
      log_error("Failed to hash file %s", anu_file_get_filename(file));
      continue;
    }

    for (int j = 0; j < config->segments; j++) {
      bk_tree_insert(&filetree, hashes[(i * config->segments) + j], i);
    }
  }

  anu_report report = anu_generate_report(&files, hashes, config, &filetree);
  anu_print_report(&report, &files);
  anu_report_destroy(&report);
  /* bk_tree_print_ascii(&filetree); */
  bk_tree_node_free(filetree.root);
  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}

int main (int argc, char *argv[]) {  // NOLINT (unused-*)
  char *path = "./etc/reference/";

  log_set_level(LOG_TRACE);

  printf("\n--------------------\n");
  printf("Starting...\n");
  printf("--------------------\n");

  anukrta_config config = {
      .segments = 4,
      .threshold = 15,
      .hash_algorithm = ANU_HASH_ALGO_DCT,
      .skip_duration = anu_time_seconds_to_microseconds(1.0)};

  log_info("%s now running...", argv[0]);
  anukrta_driver(&config, path);

  return 0;
}
