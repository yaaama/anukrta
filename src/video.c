#include "video.h"

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
#include <string.h>

#include "util.h"
#include "vendor/log.h"

/* Hardcode this so I don't have to include another header */
#define ANU_EAGAIN 11

double anu_time_microseconds_to_seconds (long microseconds) {
  return ((double) microseconds / ANU_TIME_ONE_SEC_IN_US);
}

long anu_time_seconds_to_microseconds (double seconds) {
  return (long) (seconds * (double) ANU_TIME_ONE_SEC_IN_US);
}

static long frame_pts_to_microsecond (long pts, AVRational timebase) {
  return av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

static double frame_pts_to_seconds (long pts, AVRational timebase) {
  return ((double) av_rescale_q(pts, timebase, AV_TIME_BASE_Q) / 1000000);
}

static void save_gray_frame (unsigned char *buf, int wrap, int xsize,
                             int ysize,  // NOLINT(*swappable-parameters)
                             char *prefix, long frame_num) {
  FILE *fptr;

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
    fwrite(buf + ((ptrdiff_t) index * wrap), 1, xsize, fptr);
  }
  fclose(fptr);
}

void copy_frame_to_buffer (AVFrame *frame, uint8_t *dest, int width) {
  /* Access the raw data pointer for the first plane (Y / Grayscale) */
  uint8_t *src_data = frame->data[0];
  int src_linesize = frame->linesize[0];

  for (long y = 0; y < width; y++) {
    /* Calculate the start of the row in the source frame */
    uint8_t *src_row = src_data + (y * src_linesize);

    /* Calculate the start of the row in the destination buffer */
    uint8_t *dest_row = dest + (y * width);

    memcpy(dest_row, src_row, width);
  }
}

static int normalize_colourspace (AVFrame *frame, SwsContext *context) {

  int src_range = (frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

  /* We want our output hash to use the full 0-255 range for max precision */
  int dst_range = 1;

  /* Dummy variables to retrieve default coefficients */
  int *inv_table;
  int *table;
  int dummy_src;
  int dummy_dst;
  int dummy_bright;
  int dummy_cont;
  int dummy_sat;

  // Get default values
  if (0 > sws_getColorspaceDetails(context, (&inv_table), &dummy_src, (&table),
                                   &dummy_dst, &dummy_bright, &dummy_cont,
                                   &dummy_sat)) {
    log_error("Failed to get colorspace details.");
    return -1;
  }

  /* Apply explicit ranges.
   * 1 << 16 is the fixed-point representation for "1.0" (default
   * contrast/saturation) */
  if (0 > sws_setColorspaceDetails(context, inv_table, src_range, table,
                                   dst_range, 0, 1 << 16, 1 << 16)) {
    log_error("Failed to set colourspace.");

    return -1;
  }
  return 0;
}

int anu_video_scale_frame (AVFrame *src_frame, size_t width, size_t height,
                           AVFrame *out_frame) {

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
  struct SwsContext *sws_ctx = sws_getContext(
      src_frame->width, src_frame->height, input_fmt, (int) width, (int) height,
      out_frame->format, SWS_AREA, NULL, NULL, NULL);

  if (!sws_ctx) {
    log_error("Failed to create scaling context.");
    return -1;
  }

  /* Normalise colourspaces */
  if (normalize_colourspace(src_frame, sws_ctx)) {
    log_error("Colourspace normalisation failed.");
  }

  int scaling_ret = sws_scale_frame(sws_ctx, out_frame, src_frame);
  if (scaling_ret <= 0) {
    log_error("Scaling FAILED: `%s`", av_err2str(scaling_ret));
    exit(EXIT_FAILURE);
  }

  sws_free_context(&sws_ctx);
  return 0;
}

/**
 * @brief Initialise a grayscale frame of specified width and height.
 **/
int anu_video_frame_init (int width, int height, AVFrame *out_frame) {
  out_frame->height = height;
  out_frame->width = width;
  out_frame->format = AV_PIX_FMT_GRAY8;

  if (av_frame_get_buffer(out_frame, 0) != 0) {
    av_frame_free(&out_frame);
    log_error("Could not initialise grayscale frame buffer.");
    return 1;
  }

  return 0;
}

int anu_video_decode_packet (video_io *vreader) {
  /* Send packet to decoder */
  int ret = avcodec_send_packet(vreader->codec_ctx, vreader->packet);

  /* Check if it was successful */
  if (ret != 0) {
    log_error("Failed sending packet: %s", av_err2str(ret));
    return ret;
  }

  /* Loop to pull frames
   * NOTE: A single may contain 0 frames, or MANY frames. */
  while (ret >= 0) {
    ret = avcodec_receive_frame(vreader->codec_ctx, vreader->frame);

    if (ret == AVERROR(ANU_EAGAIN) || ret == AVERROR_EOF) {
      /* Not an error. Just means we need more packets or stream is done. */
      return 0;
    }

    if (ret == AVERROR_INVALIDDATA) {
      return -1;
    }

    if (ret < 0) {
      log_error("Error receiving frame: %s", av_err2str(ret));
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

/**
 * @brief Open video and initialise video struct.
 *
 * This will open a video given by the param 'filename'.
 *
 * You need to call the complimentary function to close and destroy the struct
 * once you are done with it.
 *
 * @param filename[in] Path of file to open.
 * @param vreader[out] Struct to initialise.
 * @return 0 if success, non-zero for anything else.
 *
 */

int anu_video_open (char *filename, video_io *vreader) {

  /* Initialise VideoReader */
  vreader->fmt_ctx = NULL;
  vreader->codec_ctx = NULL;
  vreader->frame = NULL;
  vreader->packet = NULL;
  vreader->video_stream_idx = -1;
  vreader->video_duration = 0;

  log_info("Opening `%s`", filename);

  bool got_info = true;

  /* Opens input file and guesses format of file */
  int errcode = 0;
  errcode = avformat_open_input(&vreader->fmt_ctx, filename, NULL, NULL);
  if (errcode < 0) {
    log_warn("Could not open file (`%s`): `%s`", filename, av_err2str(errcode));
    log_debug("Will try to read stream information next...");
  }

  /* Will read bytes from file/decode a few frames to fill out context that the
     method above missed (`avformat_open_input` will only read header of file)
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    log_warn("Could not find stream info: `%s`", av_err2str(errcode));
    got_info = false;
  }

  if (got_info == false) {
    log_error("Failed to read header/stream for file %s", filename);
    return -1;
  }

  log_trace("Searching container for video stream...");

  /* Find Video Stream & Codec */
  bool decoder_found = true;
  bool stream_found = true;
  const AVCodec *codec = NULL;
  AVCodecParameters *codec_params = NULL;

  /* Finds best stream that matches our specifications */
  vreader->video_stream_idx = av_find_best_stream(
      vreader->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, -1);

  if (vreader->video_stream_idx == AVERROR_DECODER_NOT_FOUND) {
    log_error("No decoder found for stream.");
    decoder_found = false;
  }
  if (vreader->video_stream_idx == AVERROR_STREAM_NOT_FOUND) {
    log_error("No video stream found.");
    stream_found = false;
  }

  if (!stream_found || !decoder_found) {
    return -1;
  }

  log_trace("Found video stream at index `%d`", vreader->video_stream_idx);

  /* Get codec parameters */
  codec_params = vreader->fmt_ctx->streams[vreader->video_stream_idx]->codecpar;
  /* Get codec to decode frames */
  codec = avcodec_find_decoder(codec_params->codec_id);

  if (!codec) {
    log_error("No codec found.");
    return -1;
  }

  /* Init Codec Context */
  vreader->codec_ctx = avcodec_alloc_context3(codec);

  if (!vreader->codec_ctx) {
    log_fatal("Failed to allocate memory.");
    return -1;
  }

  if (avcodec_parameters_to_context(vreader->codec_ctx, codec_params) < 0) {
    log_error("Could not retrieve codec context.");
    return -1;
  }

  if (avcodec_open2(vreader->codec_ctx, codec, NULL) < 0) {
    log_error("Failed to initialise codec `%s`", codec->long_name);
    return -1;
  }

  /* Alloc Buffers */
  vreader->frame = av_frame_alloc();
  vreader->packet = av_packet_alloc();

  if (vreader->frame == NULL || vreader->packet == NULL) {
    log_fatal("Failed to allocate memory for packet/frame.");
    exit(EXIT_FAILURE);
  }

  vreader->video_duration = anu_video_get_duration(vreader);
  log_debug("Video duration -> %.1fs / %zu micro/s.",
            anu_time_microseconds_to_seconds(vreader->video_duration),
            vreader->video_duration);

  return 0;
}

void anu_video_close (video_io *vreader) {
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
long anu_video_get_duration (video_io *vreader) {
  AVStream *vid_stream = anu_video_get_vid_stream(vreader);

  /* duration in stream-base */
  long duration_in_sb = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;
  log_trace("Time base for stream: `%d/%d`", stream_timebase.num,
            stream_timebase.den);

  if (duration_in_sb > 0) {
    return frame_pts_to_microsecond(duration_in_sb, stream_timebase);
  }

  if (duration_in_sb == AV_NOPTS_VALUE) {
    log_warn(
        "Video stream omitting duration, using container values as fallback");
    duration_in_sb = vreader->fmt_ctx->duration;
    return duration_in_sb;
  }

  return -1;
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
int anu_video_seek_to_timestamp_pts (video_io *vreader, int64_t target_pts) {

  int ret = 0;

  /* Flush the decoder buffers.
   *   If we don't do this, the decoder might return cached frames from the
   *   old position before decoding frames from the new position. */
  avcodec_flush_buffers(vreader->codec_ctx);

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking. */
  ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx, target_pts,
                      AVSEEK_FLAG_BACKWARD);

  if (ret < 0) {
    log_warn("Error seeking to timestamp %" PRId64 ": %s", target_pts,
             av_err2str(ret));
    return ret;
  }

  return 0;
}

AVStream *anu_video_get_vid_stream (video_io *vreader) {
  assert(vreader);
  assert(vreader->video_stream_idx >= 0);
  assert(vreader->fmt_ctx);
  return vreader->fmt_ctx->streams[vreader->video_stream_idx];
}
