#include "video.h"

#include <asm-generic/errno-base.h>
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
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
   * https://en.wikipedia.org/wiki/Netpbm_format#PGM_example */
  const int header_end_marker = 255;
  fprintf(fptr, "P5\n%d %d\n%d\n", xsize, ysize, header_end_marker);

  /* writing line by line */
  int index;
  for (index = 0; index < ysize; index++) {
    fwrite(buf + ((ptrdiff_t)index * wrap), 1, xsize, fptr);
  }
  fclose(fptr);
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

int scale_frame (AVFrame* src_frame, size_t width, size_t height,
                 AVFrame* out_frame) {

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

/**
 * @brief Initialise a grayscale frame of specified width and height.
 **/
int init_gray_frame (int width, int height, AVFrame* out_frame) {
  out_frame->height = height;
  out_frame->width = width;
  out_frame->format = AV_PIX_FMT_GRAY8;

  if (av_frame_get_buffer(out_frame, 0) != 0) {
    av_frame_free(&out_frame);
    fprintf(stderr, "Could not initialise grayscale frame buffer.\n");
    return 1;
  }

  return 0;
}

int decode_packet (VideoReader* vreader) {
  /* Send packet to decoder */
  int ret = avcodec_send_packet(vreader->codec_ctx, vreader->packet);

  if (ret < 0) {
    fprintf(stderr, "Error sending packet: `%s`\n", av_err2str(ret));
    return ret;
  }

  /* Loop to pull frames
   * NOTE: A single may contain 0 frames, or MANY frames. */
  while (ret >= 0) {
    ret = avcodec_receive_frame(vreader->codec_ctx, vreader->frame);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      /* Not an error. Just means we need more packets or stream is done. */
      return 0;
    }

    if (ret == AVERROR_INVALIDDATA) {
      return -1;
    }

    if (ret < 0) {
      fprintf(stderr, "Error receiving frame: %s\n", av_err2str(ret));
      return ret;
    }

    if (ret == 0) {
      /* We have a frame. */
      /* printf("Frame number: %ld\n", vreader->codec_ctx->frame_num); */
      return 1;
    }
  }
  return ret;
}

int open_video_reader (char* filename, VideoReader* vreader) {

  /* Initialise VideoReader */
  vreader->fmt_ctx = NULL;
  vreader->codec_ctx = NULL;
  vreader->frame = NULL;
  vreader->packet = NULL;
  vreader->video_stream_idx = -1;

  printf("\n=== Opening File `%s` ===\n", filename);

  /* Open input file and read header data. */
  bool got_info = true;

  /* Opens input file and guesses format of file */
  int errcode = 0;
  errcode = avformat_open_input(&vreader->fmt_ctx, filename, NULL, NULL);
  if (errcode < 0) {
    fprintf(stderr, "Could not open file (`%s`): `%s`\n", filename,
            av_err2str(errcode));
    fprintf(stderr, "Will try to read stream information next...\n");
  }

  /* Will read bytes from file/decode a few frames to fill out context that the
     method above missed (`avformat_open_input` will only read header of file)
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    fprintf(stderr, "Could not find stream info: `%s`\n", av_err2str(errcode));
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

  if (vreader->video_stream_idx == AVERROR_STREAM_NOT_FOUND) {
    fprintf(stderr, "No video stream found.\n");
    return -1;
  }
  if (vreader->video_stream_idx == AVERROR_DECODER_NOT_FOUND) {
    fprintf(stderr, "No decoder found for stream.\n");
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
    /* Drain decoder */
    avcodec_send_packet(vreader->codec_ctx, NULL);
    avcodec_free_context(&vreader->codec_ctx);
  }
  if (vreader->fmt_ctx) {
    avformat_close_input(&vreader->fmt_ctx);
  }

  vreader = NULL;
}

long frame_pts_to_microsecond (long pts, AVRational timebase) {
  return av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

double frame_pts_to_seconds (long pts, AVRational timebase) {
  return ((double)av_rescale_q(pts, timebase, AV_TIME_BASE_Q) / 1000000);
}

/**
 * @brief Get duration of video.
 *
 * Retrieves duration of video either using container duration (if found) or by
 * using the video stream specified.
 *
 * @param fmt_ctx Format (container) context.
 * @param vid_stream Video stream.
 * @return Duration of video in microseconds.
 *
 */
long get_video_duration (AVFormatContext* fmt_ctx, AVStream* vid_stream) {

  /* duration in stream-base */
  long duration_in_sb = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;
  printf("Time base for stream: `%d/%d`\n", stream_timebase.num,
         stream_timebase.den);

  if (duration_in_sb == AV_NOPTS_VALUE) {
    fprintf(
        stderr,
        "Warning: Video stream is omitting duration. Falling back to container "
        "duration (`%ld`)\n",
        fmt_ctx->duration);

    return fmt_ctx->duration;
  }

  long duration_us =
      av_rescale_q(duration_in_sb, stream_timebase, AV_TIME_BASE_Q);
  printf("Duration of video: `%f` seconds (`%ld` micro/s)\n",
         frame_pts_to_seconds(duration_in_sb, stream_timebase), duration_us);
  return duration_us;
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
 * @param target_ts Target time stamp (in streams own time base).
 * @return 0 on success, anything else on failure.
 *
 * @note When `av_seek_frame` fails, this function returns its value.
 */
int seek_to_timestamp (VideoReader* vreader, int64_t target_pts) {

  int ret = 0;

  /* Flush the decoder buffers.
   *   If we don't do this, the decoder might return cached frames from the
   *   old position before decoding frames from the new position. */
  avcodec_flush_buffers(vreader->codec_ctx);

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking
   (default). */
  ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx, target_pts,
                      AVSEEK_FLAG_BACKWARD);

  if (ret < 0) {
    fprintf(stderr, "Error seeking to timestamp %" PRId64 ": %s\n", target_pts,
            av_err2str(ret));
    return ret;
  }

  return 0;
}
