/* Video similarity tool */
#include <assert.h>
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SAVE_HASH_PGM 0

typedef struct VideoReader {
  /* File (container/AV file) context
   * AVFormatContext holds the header information stored in file (container) */
  AVFormatContext* fmt_ctx;
  /* Video encoding context.
     Codec is used to decode the video stream */
  AVCodecContext* codec_ctx;
  /* Index of video stream inside container */
  int video_stream_idx;
  /* Packet (compressed frame of audio/video) */
  AVPacket* packet;
  /* Decoded packet */
  AVFrame* frame;
} VideoReader;

typedef enum anuHashType {
  ANUHASH_AVG = 0,
  ANUHASH_DCT = 1,
} anuHashType;

static void save_gray_frame (unsigned char* buf, int wrap, int xsize,
                             int ysize,  // NOLINT(*swappable-parameters)
                             char* prefix, long frame_num) {
  FILE* fptr;

  char filename[1024];
  snprintf(filename, sizeof(filename), "%s_frame-%ld.pgm", prefix, frame_num);
  fptr = fopen(filename, "w");

  if (!fptr) {
    perror("Failure saving gray scale image, could not open file.");
    return;
  }

  /* p5 image headers must end with 255
  https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
  */
  const int header_end_marker = 255;
  fprintf(fptr, "P5\n%d %d\n%d\n", xsize, ysize, header_end_marker);

  /* writing line by line */
  int index;
  for (index = 0; index < ysize; index++) {
    fwrite(buf + ((ptrdiff_t)index * wrap), 1, xsize, fptr);
  }
  fclose(fptr);
}

int hamming_distance (uint64_t hash1, uint64_t hash2) {
  uint64_t x =
      hash1 ^
      hash2; /* XOR finds the differences (returns 1 where bits differ) */
  int dist = 0;

  /* Count the number of 1s (Kernighan's algorithm) */
  while (x) {
    dist++;
    x &= x - 1;
  }
  return dist;
}

int normalize_colourspace (AVFrame* frame, SwsContext* context) {

  int src_range = (frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

  /* We want our output hash to use the full 0-255 range for max precision */
  int dst_range = 1;

  /* Dummy variables to retrieve default coefficients */
  int* inv_table;
  int* table;
  int dummy_src;
  int dummy_dst;
  int dummy_bright;
  int dummy_cont;
  int dummy_sat;

  // Get default values
  if (0 > sws_getColorspaceDetails(context, (&inv_table), &dummy_src, (&table),
                                   &dummy_dst, &dummy_bright, &dummy_cont,
                                   &dummy_sat)) {
    fprintf(stderr, "Failed to get colorspace details.\n");
    return -1;
  }

  /* Apply explicit ranges.
   * 1 << 16 is the fixed-point representation for "1.0" (default
   * contrast/saturation) */
  if (0 > sws_setColorspaceDetails(context, inv_table, src_range, table,
                                   dst_range, 0, 1 << 16, 1 << 16)) {
    fprintf(stderr, "Failed to set colourspace.\n");
    return -1;
  }
  return 0;
}

int scale_frame (AVFrame* src_frame, AVFrame* out_frame, size_t width,
                 size_t height) {

  enum AVPixelFormat input_fmt = src_frame->format;

  switch (input_fmt) {
    case AV_PIX_FMT_YUVJ420P:
      {
        input_fmt = AV_PIX_FMT_YUV420P;
        break;
      }
    case AV_PIX_FMT_YUVJ422P:
      {
        input_fmt = AV_PIX_FMT_YUV422P;
        break;
      }
    case AV_PIX_FMT_YUVJ444P:
      {
        input_fmt = AV_PIX_FMT_YUV444P;
        break;
      }
    default:
      break;
  }

  /* Initialize the Scaler (SwsContext) */
  /* Convert from Source Format -> Gray8 @ 8x8 */
  struct SwsContext* sws_ctx = sws_getContext(
      src_frame->width, src_frame->height, input_fmt, (int)width, (int)height,
      out_frame->format, SWS_AREA, NULL, NULL, NULL);

  if (!sws_ctx) {
    fprintf(stderr, "Failed to create SwsContext.\n");
    return -1;
  }

  /* Normalise colourspaces */
  if (normalize_colourspace(src_frame, sws_ctx)) {
    fprintf(stderr, "Colourspace normalisation returned an error...\n");
  }

  int scaling_ret = sws_scale_frame(sws_ctx, out_frame, src_frame);
  if (scaling_ret <= 0) {
    fprintf(stderr, "Scaling FAILED: `%s`", av_err2str(scaling_ret));
    exit(-1);
  }

  sws_free_context(&sws_ctx);
  return 0;
}

uint64_t average_hash (AVFrame* src_frame) {
  uint64_t hash = 0;
  int width = 8;
  int height = 8;

  /* Create small frame to hold shrunken src frame */
  AVFrame* smallframe = av_frame_alloc();
  smallframe->format = AV_PIX_FMT_GRAY8;
  smallframe->width = width;
  smallframe->height = height;

  if (av_frame_get_buffer(smallframe, 0) != 0) {
    fprintf(stderr, "Could not initialise frame.\n");
    av_frame_free(&smallframe);
  }

  if (scale_frame(src_frame, smallframe, width, height)) {
    fprintf(stderr, "Failed to scale frame!");
    exit(0);
  }

  uint8_t* pixels = smallframe->data[0]; /* Buffer for the 8x8 image */
  int linesize = smallframe->linesize[0];
  long sum = 0;

#if SAVE_HASH_PGM
  save_gray_frame(pixels, linesize, width, height, "-Gray", src_frame->pts);
#endif

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      sum += pixels[(y * linesize) + x];
    }
  }
  uint8_t avg = sum / 64;
  printf("Average sum of pixels: `%d`\n", avg);

#if SAVE_HASH_PGM
  /* Binary buffer for visualisation */
  uint8_t binary_viz[64];
#endif

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t val = pixels[(y * linesize) + x];

      /* If > avg, set to White (255), else Black (0) */

#if SAVE_HASH_PGM
      binary_viz[(y * 8) + x] = (val > avg) ? 255 : 0;
#endif

      /* Calculate position in the 64-bit integer */
      if (val > avg) {
        hash |= ((uint64_t)1 << (y * width + x));
      }
    }
  }

#if SAVE_HASH_PGM
  save_gray_frame(binary_viz, 8, width, height, "-binary", src_frame->pts);
#endif

  /* Cleanup */
  av_frame_free(&smallframe);

  return hash;
}

uint64_t dct_hash (AVFrame* frame) {
  uint64_t final_hash = 0;

  return final_hash;
}

/* This is the function called ONLY when a valid frame is fully decoded */
static void hash_decoded_frame (VideoReader* vreader, anuHashType hash_algo,
                                uint64_t* hash_out) {

  printf("Frame `%ld`, Res: `%dx%d`, PTS: `%ld`\n",
         vreader->codec_ctx->frame_num, vreader->frame->width,
         vreader->frame->height, vreader->frame->pts);

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
  *hash_out = hash;
  printf("Hash: %016" PRIx64 "\n", *hash_out);
}

int decode_packet (VideoReader* vreader) {
  // 1. Send packet to decoder
  int ret = avcodec_send_packet(vreader->codec_ctx, vreader->packet);
  if (ret < 0) {
    fprintf(stderr, "Error sending packet: `%s`\n", av_err2str(ret));
    return ret;
  }

  /* 2. Loop to pull frames (A single packet might contain multiple frames OR 0)
   */
  while (ret >= 0) {
    ret = avcodec_receive_frame(vreader->codec_ctx, vreader->frame);

    if (ret >= 0) {
      /* We have a frame. */
      return 1;
    }

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      /* Not an error. Just means we need more packets or stream is done. */
      return 0;
    }

    if (ret < 0) {
      fprintf(stderr, "Error receiving frame: %s\n", av_err2str(ret));
      return ret;
    }
  }
  return 0;
}

int open_video_reader (char* filename, VideoReader* vreader) {

  /* Initialise VideoReader */
  vreader->fmt_ctx = NULL;
  vreader->codec_ctx = NULL;
  vreader->frame = NULL;
  vreader->packet = NULL;
  vreader->video_stream_idx = -1;

  printf("Opening file `%s`", filename);

  /* Open input file and read header data. */
  bool got_info = true;

  /* Opens input file and guesses format of file */
  if (avformat_open_input(&vreader->fmt_ctx, filename, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open file (%s)\n", filename);
    fprintf(stderr, "Will try to read stream information next...\n");
  }

  /* Will read bytes from file/decode a few frames to fill out context that the
     method above missed (`avformat_open_input` will only read header of file)
   */
  if (avformat_find_stream_info(vreader->fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info!\n");
    got_info = false;
  }

  if (got_info == false) {
    fprintf(stderr, "Failed to detect file format.\n");
    return -1;
  }

  printf("\nSearching container for video stream...\n");

  /* Find Video Stream & Codec */

  const AVCodec* codec = NULL;
  AVCodecParameters* codec_params = NULL;

  /* Finds best stream that matches our specifications */
  vreader->video_stream_idx = av_find_best_stream(
      vreader->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, -1);

  if (vreader->video_stream_idx == -1) {
    fprintf(stderr, "No video stream found.\n");
    return -1;
  }

  printf("Found video stream at index `%d`\n", vreader->video_stream_idx);

  /* Get codec parameters */
  codec_params = vreader->fmt_ctx->streams[vreader->video_stream_idx]->codecpar;
  /* Get codec to decode frames */
  codec = avcodec_find_decoder(codec_params->codec_id);

  if (!codec) {
    fprintf(stderr, "No codec found?...\n");
    return -1;
  }

  /* Init Codec Context */
  vreader->codec_ctx = avcodec_alloc_context3(codec);

  if (!vreader->codec_ctx) {
    fprintf(stderr, "Failed to allocate memory.\n");
    return -1;
  }

  if (avcodec_parameters_to_context(vreader->codec_ctx, codec_params) < 0) {
    fprintf(stderr, "Could not retrieve codec context.\n");
    return -1;
  }

  if (avcodec_open2(vreader->codec_ctx, codec, NULL) < 0) {
    fprintf(stderr, "Failed to initialise codec `%s`\n", codec->long_name);
    return -1;
  }

  /* Alloc Buffers */
  vreader->frame = av_frame_alloc();
  vreader->packet = av_packet_alloc();

  if (vreader->frame == NULL || vreader->packet == NULL) {
    fprintf(stderr, "Failed to allocate memory for packet/frame.\n");
    exit(1);
  }

  return 0;
}

void close_video_reader (VideoReader* vreader) {
  if (vreader->packet) {
    av_packet_free(&vreader->packet);
  }
  if (vreader->frame) {
    av_frame_free(&vreader->frame);
  }
  if (vreader->codec_ctx) {
    avcodec_free_context(&vreader->codec_ctx);
  }
  if (vreader->fmt_ctx) {
    avformat_close_input(&vreader->fmt_ctx);
  }
}

/**
 * @brief Get duration of video.
 *
 * Retrieves duration of video either using container duration (if found) or by
 * using the video stream specified.
 *
 * @param fmt_ctx Format (container) context.
 * @param vid_stream Video stream.
 * @return Duration of video.
 *
 */
long get_video_duration (AVFormatContext* fmt_ctx, AVStream* vid_stream) {

  long duration = vid_stream->duration;
  assert(duration > 0);

  if (duration == AV_NOPTS_VALUE) {
    fprintf(stderr,
            "Video stream is omitting duration. Falling back to container "
            "duration.");

    duration =
        av_rescale_q(fmt_ctx->duration, AV_TIME_BASE_Q, vid_stream->time_base);
  }

  return duration;
}

/**
 * @brief Number of frames divided by segments.
 *
 * Returns the number of frames to seek ahead to for the next segment of video.
 **/
long calculate_frame_steps (long duration, int segments) {
  assert(duration > 0 && segments > 0);
  long frame_steps = duration / segments;
  return frame_steps;
}

/**
 * @brief Seek to timestamp.
 *
 * Seeks to a specified timestamp.
 *
 * @param vreader VideoReader instance.
 * @param target_ts Target time stamp.
 * @return 0 on success, anything else on failure.
 *
 * @note When `av_seek_frame` fails, this function will its value.
 */
int seek_to_timestamp (VideoReader* vreader, int64_t target_ts) {
  /* 1. Flush the decoder buffers.
   *   If we don't do this, the decoder might return cached frames from the
   *   old position before decoding frames from the new position. */
  avcodec_flush_buffers(vreader->codec_ctx);

  /* 2. Perform the seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking
   (default). */
  int ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx,
                          target_ts, AVSEEK_FLAG_BACKWARD);

  if (ret < 0) {
    fprintf(stderr, "Error seeking to timestamp %" PRId64 ": %s\n", target_ts,
            av_err2str(ret));
    return ret;
  }

  return 0;
}

int hash_video (char* filename, uint64_t* hashes_out, int segments,
                anuHashType hash_algo) {

  VideoReader vreader;

  /* Setup video reader */
  if (open_video_reader(filename, &vreader) < 0) {
    close_video_reader(&vreader);  // cleanup partial opens
    return -1;
  }

  printf("Opened `%s`\n", filename);

  AVStream* vid_stream_ptr = vreader.fmt_ctx->streams[vreader.video_stream_idx];
  AVFormatContext* format_context_ptr = vreader.fmt_ctx;
  long video_duration = get_video_duration(format_context_ptr, vid_stream_ptr);

  /* Loop through file packets */

  /* We want to split the video into this many segments */
  int total_video_segments = segments;
  long frame_step = calculate_frame_steps(video_duration, total_video_segments);
  /* Counter for # of frames successfully */
  int frames_decoded = 0;
  /* Target timestamp */
  long target_timestamp = 0;

  for (int i = 0; i < total_video_segments; i++) {
    target_timestamp = ((long)i * frame_step);

    printf("\n--- Segment %d/%d : Seeking to TS %" PRId64 " ---\n", i + 1,
           total_video_segments, target_timestamp);

    /* Seek to timestamp */
    if (seek_to_timestamp(&vreader, target_timestamp) < 0) {
      fprintf(stderr, "Could not seek to segment %d\n", i);
      continue;  // Try next segment
    }

    bool frame_found_for_segment = false;

    // Decode packets til we get a frame
    while (av_read_frame(vreader.fmt_ctx, vreader.packet) >= 0) {

      /* Only process video packets */
      if (vreader.packet->stream_index != vreader.video_stream_idx) {
        av_packet_unref(vreader.packet);
        continue;
      }

      int decoding_success = decode_packet(&vreader);

      if (decoding_success == 1) {

        long current_pts = vreader.frame->pts;
        if (current_pts < target_timestamp) {
          /* printf("Skipping frame at PTS %ld (Target: %ld)\n", current_pts,
           * target_timestamp); */
          av_packet_unref(vreader.packet);
          continue; /* Loop again to get next frame */
        }

        hash_decoded_frame(&vreader, hash_algo, &hashes_out[frames_decoded]);
        frame_found_for_segment = true;
        frames_decoded++;
        av_packet_unref(vreader.packet);
        break; /* Stop reading packets for this segment */
      }
      if (decoding_success < 0) {
        fprintf(stderr, "Decoding failed.\n");
        av_packet_unref(vreader.packet);
        break; /* Error */
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
int main (int argc, char* argv[]) {  // NOLINT (unused-*)
  char* filename = (argc > 1) ? argv[1] : "./tulsi.mov";
  char* filename2 = "tulsi2.mov";

  const int SEGMENTS = 4;

  uint64_t hashes_vidA[SEGMENTS];
  uint64_t hashes_vidB[SEGMENTS];

  hash_video(filename, &hashes_vidA[0], SEGMENTS, ANUHASH_AVG);
  hash_video(filename2, &hashes_vidB[0], SEGMENTS, ANUHASH_AVG);

  are_videos_duplicate(hashes_vidA, hashes_vidB, SEGMENTS);

  return 0;
}
