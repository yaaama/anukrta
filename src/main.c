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

/* This is the function called ONLY when a valid frame is fully decoded */
static uint64_t hash_decoded_frame (VideoReader* vreader,
                                    anuHashType hash_algo) {

  AVFrame* grey_frame = av_frame_alloc();
  if (grey_frame == NULL) {
    fprintf(stderr, "Failed to allocate memory for frame.\n");
    abort();
  }

  /* Create an emtpty grey frame */
  if (init_grey_frame(ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE, grey_frame)) {
    fprintf(stderr, "Failed to initialise frame.\n");
    abort();
  }

  /* Scale frame down to 32x32 and store in empty grey frame */
  if (scale_frame(vreader->frame, ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                  grey_frame)) {
    fprintf(stderr, "Failed to scale frame!");
    /* Clean up before aborting */
    av_frame_free(&grey_frame);
    abort();
  }

  /* Generate a 2D matrix of the greyscale values */
  float matrix[ANU_PHASH_INPUT_SIZE][ANU_PHASH_INPUT_SIZE] = {0};

  /* Populate matrix with frame data */
  for (int y = 0; y < ANU_PHASH_INPUT_SIZE; y++) {
    uint8_t* row_ptr =
        grey_frame->data[0] + ((ptrdiff_t)y * grey_frame->linesize[0]);
    for (int x = 0; x < ANU_PHASH_INPUT_SIZE; x++) {
      matrix[y][x] = row_ptr[x];
    }
  }

  uint64_t hash = 0;
  switch (hash_algo) {
    case ANU_HASH_ALGO_DCT:
      {
        hash = dct_hash(&matrix[0][0]);
        break;
      }
    default:
      {
        fprintf(stderr, "Hashing algorithm not specified.");
      }
  }

  if (hash == 0) {
    fprintf(stderr, "Received a 0 value for hash.");
  }

  av_frame_free(&grey_frame);
  return hash;
}

int hash_video (char* filename, anuHashType hash_algo, int segments,
                uint64_t* hashes_out) {

  if (segments <= 0) {
    printf("Skipping hash for `%s`\n", filename);
    return 0;
  }
  VideoReader vreader;

  /* Setup video reader */
  if (open_video_reader(filename, &vreader) < 0) {
    close_video_reader(&vreader);  // cleanup partial opens
    return -1;
  }

  /* Container: vreader.fmt_ctx; */

  /* Video stream */
  AVStream* vid_stream_ptr = vreader.fmt_ctx->streams[vreader.video_stream_idx];

  /* Loop through file packets */

  /* We want to split the video into this many segments */
  int total_video_segments = segments;
  long video_duration_us = get_video_duration(vreader.fmt_ctx, vid_stream_ptr);
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

    printf("--- Segment %d/%d : Seeking to PTS %" PRId64 " (%.3f sec) ---\n",
           i + 1, total_video_segments, seek_target_sb,
           (double)seek_target_us / 1000000.0);

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

        printf("\tHashing Frame `%ld`, PTS: `%ld`",
               vreader.codec_ctx->frame_num,
               vreader.frame->best_effort_timestamp);
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

int are_videos_duplicate (uint64_t* hashesA, uint64_t* hashesB,
                          uint64_t segments) {
  if (segments < 1) {
    return 0;
  }
  uint64_t total_distance = 0;
  uint64_t total_bits = (uint64_t)(segments * 64); /* 64 bits per hash */

  printf("\nCOMPARISON REPORT\n");
  printf("%-10s | %-16s | %-16s | %s\n", "Segment", "Hash A", "Hash B",
         "Distance");
  printf("-----------|------------------|------------------|---------\n");
  for (uint64_t i = 0; i < segments; i++) {
    uint64_t dist = hamming_distance(hashesA[i], hashesB[i]);
    total_distance += dist;
    printf("%-10lu | %016" PRIx64 " | %016" PRIx64 " | %lu\n", i, hashesA[i],
           hashesB[i], dist);
  }

  /* Calculate similarity percentage */
  /* 1.0 means identical, 0.0 means completely opposite */

  float similarity = 1 - ((float)total_distance / (float)total_bits);

  printf("\nTotal Hamming Distance: %lu / %lu bits\n", total_distance,
         total_bits);

  printf("Similarity Score:\t\t%.2f%%\n", (double)(similarity * 100.0F));

  /* DECISION THRESHOLD */
  /* For pHash (8x8), a distance of <= 10 on a single image is usually a match.
   For 4 segments (256 bits total), a safe threshold is usually around 10-15%
   difference. */

  const int THRESHOLD = 20;

  if (total_distance <= THRESHOLD) {
    printf("VERDICT: DUPLICATES (High confidence)\n");
    return 1;
  }
  printf("VERDICT: DIFFERENT VIDEOS\n");
  return 0;
}

int main (int argc, char* argv[]) {  // NOLINT (unused-*)
  const int SEGMENTS = 3;
  anuFileQ files;
  anu_fileq_init(&files, 50);

  char* path = "./etc";
  size_t file_count = anu_recursive_filewalk(path, &files);

  if (file_count < 1) {
    fprintf(stderr, "Detected no video files.\n");
    return -1;
  }

  uint64_t* hashes = calloc((file_count * SEGMENTS), sizeof(uint64_t));
  if (!hashes) {
    abort();
  }

  anuFile* file;

  for (size_t i = 0; i < file_count; i++) {
    file = &files.items[i];
    uint64_t* current_file_hashes = &hashes[i * SEGMENTS];
    hash_video(file->path, ANU_HASH_ALGO_DCT, SEGMENTS, current_file_hashes);
  }

  printf("\n\n========================================\n");
  printf("%-10sFINAL HASH REPORT\n", " ");
  printf("%-10sPath: `%s`\n", " ", path);
  printf("========================================\n");

  for (size_t i = 0; i < file_count; i++) {
    file = &files.items[i];
    printf("[%zu] | %-20s\n", (i + 1), file->name);
    for (int frame = 0; frame < SEGMENTS; frame++) {
      size_t index = (i * SEGMENTS) + frame;
      printf("\tSegment %d: 0x%016" PRIx64 "\n", frame + 1, hashes[index]);
    }
    printf("----------------------------------------\n");
  }

  anu_fileq_destroy(&files);
  free(hashes);

  return 0;
}
