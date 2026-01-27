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
#include "stack.h"
#include "util.h"
#include "video.h"


/* This is the function called ONLY when a valid frame is fully decoded */
static uint64_t hash_decoded_frame (VideoReader* vreader,
                                    anuHashType hash_algo) {

  printf("Hashing Frame `%ld`, PTS: `%ld`\n", vreader->codec_ctx->frame_num,
         vreader->frame->best_effort_timestamp);

  uint64_t hash = 0;
  switch (hash_algo) {
    case ANUHASH_AVG:
      {
        hash = average_hash(vreader->frame);
        break;
      }
    case ANUHASH_DCT:
      {
        hash = dct_hash(vreader->frame);
        break;
      }
    default:
      {
        fprintf(stderr, "Hashing algorithm not specified.");
        exit(1);
      }
  }

  if (hash == 0) {
    fprintf(stderr, "Received a 0 value for hash.");
  }

  printf("Hash: %016" PRIx64 "\n", hash);
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

  /* Container */
  AVFormatContext* format_context_ptr = vreader.fmt_ctx;
  /* Video stream */
  AVStream* vid_stream_ptr = vreader.fmt_ctx->streams[vreader.video_stream_idx];

  /* Loop through file packets */

  /* We want to split the video into this many segments */
  int total_video_segments = segments;
  long video_duration_us =
      get_video_duration(format_context_ptr, vid_stream_ptr);
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

        hashes_out[frames_decoded] = hash_decoded_frame(&vreader, hash_algo);
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
  if (segments <= 0) {
    return 0;
  }
  uint64_t total_distance = 0;
  uint64_t total_bits = (uint64_t)(segments * 64); /* 64 bits per hash */

  printf("\nCOMPARISON REPORT\n");
  printf("%-10s | %-16s | %-16s | %s\n", "Segment", "Hash A", "Hash B",
         "Distance");
  printf("-----------|------------------|------------------|---------\n");
  uint64_t dist = 0;
  for (uint64_t i = 0; i < segments; i++) {
    dist = hamming_distance(hashesA[i], hashesB[i]);
    total_distance += dist;
    printf("%-10lu | %016" PRIx64 " | %016" PRIx64 " | %lu\n", i, hashesA[i],
           hashesB[i], dist);
  }

  /* Calculate similarity percentage */
  /* 1.0 means identical, 0.0 means completely opposite */

  float similarity = 1 - ((float)total_distance / (float)total_bits);

  printf("\nTotal Hamming Distance: %lu / %lu bits\n", total_distance,
         total_bits);
  printf("Similarity Score:\t\t%.2f%%\n", similarity * 100);

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
  char* filename = "./etc/tulsi.mov";
  /* char* filename2 = "./etc/tulsi_bad.mov"; */
  /* char* filename2 = "./etc/tulsi_shortened.mkv"; */
  char* filename2 = "./etc/cow.mov";

  const int SEGMENTS = 0;

  uint64_t hashes_vidA[ANU_MAX_VIDEO_SEGMENTS];
  uint64_t hashes_vidB[ANU_MAX_VIDEO_SEGMENTS];

  hash_video(filename, SEGMENTS, ANUHASH_DCT, &hashes_vidA[0]);
  hash_video(filename2, SEGMENTS, ANUHASH_DCT, &hashes_vidB[0]);
  are_videos_duplicate(hashes_vidA, hashes_vidB, SEGMENTS);

  return 0;
}
