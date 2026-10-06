#include "video.h"

#include <assert.h>
#include <errno.h> /* IWYU pragma: keep */
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avstring.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "hash.h"
#include "log.h"
#include "signals.h"
#include "util.h"

/* 24 is usually a safe threshold for limited-range YUV "black" */
#define AK_DEFAULT_BLACK_THRESHOLD 24
/* (255 - 24): symmetric white cut-off. Catches limited-range "white" pixels. */
#define AK_DEFAULT_WHITE_THRESHOLD (255 - AK_DEFAULT_BLACK_THRESHOLD)

/* video.c private error domain. Must go through ak_errstr() to print. */
#define AK_ERR_FRAME_BLACK FFERRTAG('B', 'L', 'K', 'F') /* Completely black frame */

/* Wrapper for av_strerror with our own custom errors */
static const char *ak_errstr (int err, char *buf, size_t buflen) {
  switch (err) { /* NOLINT */
    case AK_ERR_FRAME_BLACK:
      return "frame contains no content";
    default:
      return av_strerror(err, buf, buflen) < 0 ? "unknown error" : buf;
  }
}

/* writes error string into caller-provided storage */
static inline char *ak_errstr_r (int err, char *buf, size_t buflen) {
  switch (err) { /* NOLINT */
    case AK_ERR_FRAME_BLACK:
      av_strlcpy(buf, "frame contains no content", buflen);
      break;
    /* future AK_ERR_* tags: add a case here */
    default:
      if (av_strerror(err, buf, buflen) < 0) {
        av_strlcpy(buf, "unknown error", buflen);
      }
      break;
  }
  return buf;
}

/* ak_err2str-style wrapper */
#define ak_err2str(err) ak_errstr_r((err), (char[AV_ERROR_MAX_STRING_SIZE]){0}, AV_ERROR_MAX_STRING_SIZE)

typedef struct vreader {
  /* File (container/AV file) context
   * AVFormatContext holds the header information stored in file (container) */
  AVFormatContext *fmt_ctx;
  /* Video encoding context.
     Codec is used to decode the video stream */
  AVCodecContext *codec_ctx;
  /* Scaling context (cached for performance) */
  SwsContext *sws_ctx;
  /* Packet (compressed frame of audio/video) */
  AVPacket *packet;
  /* Decoded packet */
  AVFrame *frame;
  /* Filename (used for debugging messages) */
  char *fname;
  /* cached full-res GRAY8 frame */
  AVFrame *grey_frame;
  /* Cached swscaler used to convert pixel fmt to GRAY8. */
  SwsContext *grey_sws_ctx;
  /* Index of video stream inside container */
  int video_stream_idx;
} vreader;

typedef struct crop_region {
  int left;
  int top;
  int right;
  int bottom;
} crop_region;

typedef struct filter_ctx {
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
  bool init;
} filter_ctx;

/**
 * libav calls this during BLOCKING I/O (file reads in av_read_frame,
 * avformat_find_stream_info, seeks, etc). Returning non-zero aborts the
 * operation with AVERROR_EXIT.
 *
 * @note Only interrupts I/O, not pure CPU decode, but since we seek per
 * segment, I/O is where the time goes.
 */
static int av_interrupt_cb (void *opaque) {
  const ak_signals_ctx *signals = opaque;
  return (signals && ak_shutdown_requested(signals)) ? 1 : 0;
}

/**
 * Destructor for vreader.
 *
 * @param [in] vreader vreader to destroy.
 */
static void vreader_close (vreader *vreader) {
  if (!vreader) {
    return;
  }
  av_packet_free(&vreader->packet);
  sws_freeContext(vreader->sws_ctx);
  sws_freeContext(vreader->grey_sws_ctx);
  av_frame_free(&vreader->frame);
  av_frame_free(&vreader->grey_frame);
  avcodec_free_context(&vreader->codec_ctx);
  avformat_close_input(&vreader->fmt_ctx);
}

/**
 * Auto-cleanup helper for vreader.
 */
AK_DEFINE_AUTO(vreader_close, vreader, vreader_close(ak__obj))

/**
 * Helper function to retreive video stream from an initialised vreader.
 *
 * @return Pointer to video stream (AVStream).
 */
static AK_ALWAYS_INLINE AK_NONNULL_ARG(1) AVStream *vreader_video_stream (vreader *vreader) {
  return vreader->fmt_ctx->streams[vreader->video_stream_idx];
}

/**
 * Helper function to return the file URL from an initialised vreader.
 *
 * @return The URL of the file as a char pointer.
 */
static AK_ALWAYS_INLINE AK_NONNULL_ARG(1) char *vreader_fmt_url (vreader *vreader) {
  return vreader->fmt_ctx->url;
}

/**
 * Convert a PTS from a specified timebase to MICROSECONDS.
 *
 * @param pts PTS value.
 * @param timebase Timebase that PTS is currently using.
 * @return PTS value in microseconds (useconds) or AV_NOPTS_VALUE if pts is invalid.
 */
static AK_ALWAYS_INLINE AK_CONST int64_t pts_to_useconds (int64_t pts, AVRational timebase) {
  return (pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

/**
 * Convert a PTS from a specified timebase to SECONDS.
 *
 * @param pts PTS value.
 * @param timebase Timebase that PTS is currently using.
 * @return PTS value in seconds or AV_NOPTS_VALUE if pts is invalid.
 */
static AK_ALWAYS_INLINE AK_CONST double pts_to_seconds (int64_t pts, AVRational timebase) {
  return (pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : (double) pts * av_q2d(timebase);
}

/**
 * Retrieve a sane PTS value from frame.
 *
 * @param [in]frame Frame to retrieve PTS for.
 * @return The PTS in the streams timebase OR if pts is not available,
 * then the frames best effort timestamp (also in stream timebase).
 */
static AK_ALWAYS_INLINE AK_PURE int64_t get_frame_pts (const AVFrame *frame) {
  return (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->best_effort_timestamp;
}

/**
 * Normalise an angle to between 0 and 360 degrees.
 *
 * @param angle Input angle (in degrees).
 * @return Normalised angle (degrees).
 */
static AK_ALWAYS_INLINE AK_CONST int normalise_angle_360 (const int angle) {
  return (((angle % 360) + 360) % 360);
}

/**
 * Check metadata of stream for display transformations (rotations).
 *
 * @return Rotation angle between -180 and 180 degrees (if found).
 * @retval 0 if no rotation data.
 */
static AK_NONNULL_ALL int get_video_stream_rotation (const AVStream *stream) {
  /* Search the side data array inside the streams codec parameters */
  const AVPacketSideData *sd = av_packet_side_data_get(
      stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);

  if (!sd) {
    return 0;
  }

  int32_t *display_matrix = (int32_t *) sd->data;
  return (int) av_display_rotation_get(display_matrix);
}

/**
 * Checks if detect_black_borders() can walk data[0] directly for @p fmt.
 *
 * True for formats such as GRAY8 and 8-bit planar/semi-planar YUV (yuv420p, nv12, yuva...).
 * False for RGB (GBRP has G in plane 0; packed RGB is interleaved), packed
 * YUV (YUYV/UYVY), high bit depth, palette and hardware formats.
 */
static bool luma_is_u8_plane0 (enum AVPixelFormat fmt) {
  const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(fmt);
  if (!d) {
    return false;
  }
  if (d->flags &
      (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_HWACCEL)) {
    return false;
  }
  if (d->nb_components > 1 && !(d->flags & AV_PIX_FMT_FLAG_PLANAR)) {
    return false; /* packed YUV: luma interleaved with chroma */
  }
  return (d->comp[0].depth == 8) && (d->comp[0].plane == 0);
}

/**
 * @brief Open video and initialise video struct.
 *
 * This will open a video given by the param 'filename'.
 *
 * You need to call the complimentary function to close and destroy the struct
 * once you are done with it.
 *
 * @param f_path File path.
 * @param vreader Video reader to initialise. `vreader` must already be allocated.
 * @return AK_OK if success, anything else is an error.
 *
 */
static AK_NONNULL_ARG(1, 2, 3) int vreader_init (const char *f_path,
                                                 ak_signals_ctx *signals,
                                                 vreader *vreader) {

  /* Assign video stream index to an invalid index by default */
  vreader->video_stream_idx = -1;

  int errcode = 0;

  /*
   * Initialise FORMAT CONTEXT.
   * This step will check if file is existent, can be opened, etc.
   */
  vreader->fmt_ctx = AK_OOM(avformat_alloc_context());

  vreader->fmt_ctx->interrupt_callback = (AVIOInterruptCB){.callback = av_interrupt_cb, .opaque = signals};
  /* Opens input file and guesses format of file */
  errcode = avformat_open_input(&vreader->fmt_ctx, f_path, NULL, NULL);

  if (errcode != 0) {
    log_error("[%s] Could not open file: (%s)", f_path, ak_err2str(errcode));
    return errcode;
  }

  /*
   * Read bytes from file / decode a few frames to fill out context that the
     method above missed.
   * `avformat_open_input` will only read header of file (which may not always be accurate).
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    log_error("[%s] Failed to read both file header and stream info. (%s)", f_path, ak_err2str(errcode));
    return errcode;
  }

  /*
   * FIND VIDEO STREAM AND DECODER FOR IT
   * Find video stream stored in file.
   * Stores decoder for that video stream in `codec`.
   * Return value of `av_find_best_stream` is the stream index that we store in our struct.
   */
  const AVCodec *codec = NULL;

  vreader->video_stream_idx = av_find_best_stream(vreader->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, -1);

  /* Check to see if we successfully got the video stream */
  if (vreader->video_stream_idx < 0) {
    if (vreader->video_stream_idx == AVERROR_DECODER_NOT_FOUND) {
      log_error("[%s] No decoder found for stream.", f_path);
    } else if (vreader->video_stream_idx == AVERROR_STREAM_NOT_FOUND) {
      log_error("[%s] No video stream found.", f_path);
    } else {
      log_error("[%s] Failed to find best stream: %s", f_path, ak_err2str(vreader->video_stream_idx));
    }
    return errcode;
  }

  /* Check for whether the codec was set by `av_find_best_stream()` */
  if (!codec) {
    log_error("[%s] No codec found for stream.", f_path);
    return AVERROR_DECODER_NOT_FOUND;
  }

  AVStream *vid_stream = vreader_video_stream(vreader);

  log_trace("[%s] Video stream idx: [%d]", f_path, vreader->video_stream_idx);

  /* Discard ALL non-video streams */
  for (unsigned int i = 0; i < vreader->fmt_ctx->nb_streams; i++) {
    if (i != (unsigned int) vreader->video_stream_idx) {
      vreader->fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    }
  }
  log_trace("discarded %u non-video stream(s), keeping #%d", vreader->fmt_ctx->nb_streams - 1,
            vreader->video_stream_idx);

  /* Allocate Codec Context */
  AVCodecContext *codec_ctx = AK_OOM(avcodec_alloc_context3(codec));

  vreader->codec_ctx = codec_ctx;

  /* Get codec parameters required for video stream */
  AVCodecParameters *codec_params = vid_stream->codecpar;
  errcode = avcodec_parameters_to_context(vreader->codec_ctx, codec_params);
  if (errcode < 0) {
    log_error("[%s] Could not initialise codec with supplied parameters (%s)", f_path, ak_err2str(errcode));
    return errcode;
  }

  /*
   * Settings for our decoder
   * NOTE: This must come AFTER `avcodec_parameters_to_context` so that our overrides aren't overriden
   */

  codec_ctx->thread_count = 1; /* NOTE: Set thread count to prevent CACHE THRASHING */
  vreader->codec_ctx->skip_loop_filter = AVDISCARD_ALL; /* Disable applying filter to speed up decoding */
  vreader->codec_ctx->flags2 |= AV_CODEC_FLAG2_FAST; /* Enable speedup tricks whilst decoding the video */
  vreader->codec_ctx->skip_frame = AVDISCARD_NONREF; /* Skip frames that are not reference frames */

  /* Initialise the codec context for use with our codec */
  errcode = avcodec_open2(vreader->codec_ctx, codec, NULL);
  if (errcode < 0) {
    log_error("[%s] Failed to initialise codec context %s (%s)", f_path, codec->long_name,
              ak_err2str(errcode));
    return errcode;
  }

  /* Alloc Buffers */
  vreader->frame = AK_OOM(av_frame_alloc());
  vreader->packet = AK_OOM(av_packet_alloc());

  return 0;
}

/**
 * Get duration of video in microseconds.
 *
 * Retrieves duration of video either by using the video stream or falling back to container.
 *
 * @return Duration of video in microseconds.
 *
 */
static inline AK_NONNULL_ALL i64 vreader_get_duration (vreader *vreader) {

  AVStream *vid_stream = vreader_video_stream(vreader);

  /* duration in stream-base */
  int64_t duration = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;

  /* If duration is without a value then we get the container provided duration */
  if (duration == AV_NOPTS_VALUE) {

    /* NOTE: Container durations are in microseconds (AV_TIME_BASE) */
    duration = vreader->fmt_ctx->duration > 0 ? vreader->fmt_ctx->duration : 0;
    log_info(
        "[%s] Video stream omitting duration, using container values as "
        "fallback (%.2fs)",
        vreader->fname, ak_time_microsec_sec(duration));
    return duration;
  }

  /* If duration is larger than 0 then convert stream timebase duration to microseconds (AV_TIME_BASE) */
  return duration > 0 ? pts_to_useconds(duration, stream_timebase) : 0;
}

/**
 * Seek to timestamp.
 *
 * Seeks to nearest preceding keyframe from target timestamp.
 *
 * @param vreader VideoReader instance.
 * @param target_pts_streambase Target time stamp (in streams own time base).
 * @return 0 on success, libav's error code on on failure.
 */
static int vreader_seek_pts (vreader *vreader, int64_t target_pts_streambase) {

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking. */
  int ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx, target_pts_streambase,
                          AVSEEK_FLAG_BACKWARD);

  if (ret < 0) {
    return ret;
  }

  /* Flush the decoder buffers after a SUCCESSFUL seek.
   * If we don't do this, the decoder might return cached frames from the
   * old position before decoding frames from the new position. */
  avcodec_flush_buffers(vreader->codec_ctx);
  return 0;
}

/**
 * @brief Get a video frame.
 * @param [in] vreader An instance of a vreader.
 * @return Integer.
 * @retval 0 When successfully decoding packet.
 * @retval -1 End of file.
 * @retval -11 Error, please try again.
 * @retval Anything else is an unknown error.
 */
static int vreader_decode_frame (vreader *vreader) {
  int ret = 0;
  AVCodecContext *codec_ctx = vreader->codec_ctx;

  for (;;) {
    /* Try to grab a decoded frame first */
    ret = avcodec_receive_frame(codec_ctx, vreader->frame);

    if (ret >= 0) {
      /* Success: We have a frame */
      return 0;
    }
    if (ret == AVERROR_EOF) {
      /* EOF reached */
      return ret;
    }
    if (ret != AVERROR(EAGAIN)) {
      /* Fatal decoding error */
      log_error("[%s] Error receiving frame: %s", vreader->fname, ak_err2str(ret));
      return ret;
    }

    /* EAGAIN means the decoder needs more data. Read a packet. */
    ret = av_read_frame(vreader->fmt_ctx, vreader->packet);
    if (ret == AVERROR_EOF) {
      /* Flush the decoder and loop back to receive the remaining frames */
      ret = avcodec_send_packet(codec_ctx, NULL);

      if (ret == AVERROR(ENOMEM)) {
        return ret;
      };

      continue;
    }

    if (ret == AVERROR_EXIT) {
      return ret;
    }

    if (ret < 0) {
      log_warn("[%s] Decoding error: %s", vreader->fname, ak_err2str(ret));
      return ret;
    }

    /* Send the correct video packet to the decoder */
    ret = avcodec_send_packet(codec_ctx, vreader->packet);

    av_packet_unref(vreader->packet);

    if (ret == AVERROR(ENOMEM) || ret == AVERROR_EXIT) {
      return ret;
    }

    if (ret < 0) {
      log_warn("[%s] Decoding error: %s", vreader->fname, ak_err2str(ret));
      return ret;
    }

    /* Loop back to step 1 to receive the frame we just pushed data for */
  }
}

/**
 * Seeks video to target pts, and then decodes forward til target is reached or PTS is > min pts.
 *
 * @param vreader Video reader.
 * @param target_pts_streambase Target pts to reach.
 * @param min_pts_streambase Minimum value of PTS to reach before returning.
 * @return 0 if success, AV_ERR_ on failure.
 *
 */
static int vreader_seek_decode_to_target (vreader *vreader,
                                          int64_t target_pts_streambase,
                                          int64_t min_pts_streambase) {

  AK_ASSUME(target_pts_streambase >= 0);
  int ret = vreader_seek_pts(vreader, target_pts_streambase);
  if (ret != 0) {
    return ret;
  }

  for (;;) {
    ret = vreader_decode_frame(vreader);
    if (ret != 0) {
      return ret; /* EOF or decoding error */
    }

    int64_t current_pts_sb = get_frame_pts(vreader->frame);

    /* Check if we reach desired target pts OR we reach a frame higher than minimum pts */
    if ((current_pts_sb >= target_pts_streambase) && (current_pts_sb > min_pts_streambase)) {
      return 0;
    }
  }
}

/**
 * Check whether a scanline contains any non-black pixels.
 *
 * @param row First pixel in our row (luma plane).
 * @param width Number of pixels in row (must be >= 0).
 * @param threshold Black cut-off.
 *                  A threshold >= 255 makes ALL rows report as being 'black' (false);
 *                  0 means 'any non-zero pixel'.
 *
 * @return true if at least one pixel exceeds @p threshold.
 */
static AK_ALWAYS_INLINE AK_PURE AK_NONNULL_ARG(1) bool row_has_video (const uint8_t *const restrict row,
                                                                      const int width,
                                                                      const int threshold) {
  AK_ASSUME(width >= 0);
  AK_ASSUME(threshold >= 0);

  /* Stores brightest value we encounter */
  uint8_t brightest = 0;

  for (int i = 0; i < width; i++) {
    /* Store brightest pixel we encounter. */
    brightest = (row[i] > brightest) ? row[i] : brightest;
  }

  return (int) brightest > threshold;
}

/**
 * Find the bounding box of non-black pixels in a frames picture.
 *
 * @todo Replace this with an libav function later.
 *
 * Reads the luma plane only, in three passes:
 *   - Top edge    - walk rows downward until one contains video.
 *   - Bottom edge - walk rows upward from the last row. Guaranteed to
 *                    stop at or above `top`, since row `top` has video.
 *   - Side edges  - only for rows between top and bottom, shrink the
 *                    left/right margins. Each row re-scans just the
 *                    still-unchecked margins, and the pass stops early
 *                    once the picture touches both frame edges.
 *
 * @param frame     Read-only frame to inspect. Only data[0]/linesize[0] are read;
 * @param threshold Black cut-off, see row_has_video().
 * @param crop_out  Output rectangle (should be initialised to 0).
 *                  On success, `left`/`top` hold the coordinates of the first content pixel and
 *                  `width`/`height` the extent of the content area.
 *                  Left unmodified when the frame is fully black.
 *
 * @return true when content was found, false for a fully black frame.
 *
 * @warn This function expects frame->data[0] to be the luma plane. This holds true for YUV and GRAY pixel
 * formats, NOT RGB.
 */
static AK_NONNULL_ARG(1, 3) bool detect_black_borders (const AVFrame *frame,
                                                       const int threshold,
                                                       crop_region *const restrict crop_out) {

  const int w = frame->width;
  const int h = frame->height;
  const ptrdiff_t linesize = frame->linesize[0];
  const uint8_t *const restrict y_plane = frame->data[0];

  /* top edge.
   * `row` walks down one scanline per iteration and ends pointing at the first content row  */
  int top = 0;
  const uint8_t *row_ptr = y_plane;
  while ((top < h) && !row_has_video(row_ptr, w, threshold)) {
    top++;
    row_ptr += linesize;
  }

  /* Return false if every pixel in frame was black. */
  if (top == h) {
    return false;
  }

  /* bottom edge.
   * NOTE: We know there is a non-black pixel here somewhere (or we would have returned early).
   * Therefore we do not need to check if bottom > top.
   */
  int bottom = h - 1;
  const uint8_t *bottom_ptr = y_plane + ((ptrdiff_t) bottom * linesize);
  /* Find bottom bound */
  while (!row_has_video(bottom_ptr, w, threshold)) {
    bottom--;
    bottom_ptr -= linesize;
  }

  int left = w - 1;
  int right = 0;

  for (int y = top; y <= bottom; y++, row_ptr += linesize) {

    /* Find the first non-black pixel from the left */
    /* We only need to check up to our current known 'left' */
    for (int x = 0; x < left; x++) {
      if (row_ptr[x] > threshold) {
        left = x;
        break;
      }
    }

    /* Find the first non-black pixel from the right */
    /* We only need to check down to our current known 'right' */
    for (int x = w - 1; x > right; x--) {
      if (row_ptr[x] > threshold) {
        right = x;
        break;
      }
    }

    /* Early exit if we hit the absolute edges of the frame */
    if (left == 0 && right == w - 1) {
      break;
    }
  }

  crop_out->left = left;
  crop_out->top = top;
  /* left/right/top/bottom are all INCLUSIVE -> +1 for extent. */
  AK_ASSUME(((right - left) + 1) > 0);
  AK_ASSUME(((bottom - top) + 1) > 0);
  crop_out->right = (right - left) + 1;
  crop_out->bottom = (bottom - top) + 1;
  return true;
}

/**
 * @brief Produce hash from a video frame.
 * @param matrix 1D array of pixel values.
 * @param hash_algo TODO The type of hashing algorithm to use. Currently does not do anything.
 * @return Unsigned 64 bit integer (hash).
 */
static AK_PURE uint64_t hash_decoded_frame (const uint8_t *restrict matrix, const ak_hash_type hash_algo) {

  if (hash_algo != AK_HASH_ALGO_DCT) {
    AK_TODO("We've only implemented DCT hashing thus far.");
  }

  uint64_t hash = 0;
  hash = dct_hash(matrix);

  return hash;
}

/**
 * @brief Prepare software scaler by normalising colourspace details.
 *
 * @param context Software scaler instance.
 * @param src_range The input's colourspace range.
 *
 * @return int
 * @retval 0 Success.
 * @retval AV_ERROR_* when failure to get or set the software scaler's colourspace.
 */
static int normalise_sws_colourspace (SwsContext *context, int src_range) {

  /* We want our output hash to use the full 0-255 range for max precision */
  int dst_range = 1;

  /* Dummy variables to retrieve default coefficients */
  int *inv_table;
  int *table;
  int curr_src;
  int curr_dst;
  int brightness;
  int contrast;
  int saturation;

  int ret = 0;
  ret = sws_getColorspaceDetails(context, &inv_table, &curr_src, &table, &curr_dst, &brightness, &contrast,
                                 &saturation);
  /* Get default values */
  if (ret < 0) {
    return ret;
  }

  /* Return early if source and dest ranges are the same */
  if (curr_src == src_range && curr_dst == dst_range) {
    return 0;
  }

  /* Apply explicit ranges. */
  ret = sws_setColorspaceDetails(context, inv_table, src_range, table, dst_range, brightness, contrast,
                                 saturation);
  if (ret < 0) {
    return ret;
  }
  return 0;
}

/**
 * Checks if an AVPixelFormat is Greyscale or RGB.
 */
static inline AK_PURE bool is_color_matrix_applicable (enum AVPixelFormat const fmt) {
  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
  if (!desc) {
    return false;
  }

  /* If it's RGB or has less than 3 components (like GRAY8), YUV matrices don't apply */
  if ((ak_flag_has(desc->flags, AV_PIX_FMT_FLAG_RGB)) || (desc->nb_components < 3)) {
    return false;
  }
  return true;
}

/**
 * Map deprecated "J" formats to standard formats and force full range pixel format.
 *
 * This is required otherwise ffmpeg will give us the warning:
 * `deprecated pixel format used, make sure you did set range correctly`
 */
static inline void standardise_pixel_format (const AVFrame *src,
                                             enum AVPixelFormat *restrict out_fmt,
                                             int *restrict out_range) {
  *out_fmt = src->format;
  *out_range = (src->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

  switch (src->format) {
    case AV_PIX_FMT_YUVJ420P:
      *out_fmt = AV_PIX_FMT_YUV420P;
      *out_range = 1;
      break;
    case AV_PIX_FMT_YUVJ422P:
      *out_fmt = AV_PIX_FMT_YUV422P;
      *out_range = 1;
      break;
    case AV_PIX_FMT_YUVJ444P:
      *out_fmt = AV_PIX_FMT_YUV444P;
      *out_range = 1;
      break;
    case AV_PIX_FMT_YUVJ440P:
      *out_fmt = AV_PIX_FMT_YUV440P;
      *out_range = 1;
      break;
    default:
      break;
  }
}

/**
 * Detects black borders and applies cropping to the AVFrame.
 */
static int apply_crop (vreader *vr, AVFrame *frame, const int threshold_black, const int threshold_white) {

  (void) threshold_white; /* Reserved for white-bar detection; Not yet implemented. */
  AVFrame *const src = frame;
  AVStream *const stream = vreader_video_stream(vr);
  const i64 frame_pts = get_frame_pts(src);

  const int threshold = (threshold_black > 0) ? threshold_black : AK_DEFAULT_BLACK_THRESHOLD;
  crop_region crop = {0};

  /* Detect black border around video pixels */
  if (!detect_black_borders(src, threshold, &crop)) {
    log_info("[%s] Frame (#%" PRId64 ") is completely black.", vr->fname,
             pts_to_useconds(frame_pts, stream->time_base));
    return AK_ERR_FRAME_BLACK;
  }

  const size_t c_left = (size_t) crop.left;
  const size_t c_top = (size_t) crop.top;
  const size_t c_right = (size_t) (src->width - crop.right - crop.left);
  const size_t c_bottom = (size_t) (src->height - crop.bottom - crop.top);

  /* No borders detected -> leave the frame untouched. */
  if (!(c_left | c_top | c_right | c_bottom)) {
    return 0;
  }

  if (c_left || c_top || c_right || c_bottom) {
    log_debug("[%s] Cropping frame (%.3f s) from %dx%d, removing L:%zu T:%zu R:%zu B:%zu", vr->fname,
              pts_to_seconds(frame_pts, stream->time_base), src->width, src->height, c_left, c_top, c_right,
              c_bottom);
  }

  src->crop_left = c_left;
  src->crop_top = c_top;
  src->crop_right = c_right;
  src->crop_bottom = c_bottom;

  /* NOTE: The only flag recognised by `av_frame_apply_cropping`
   * is `AV_FRAME_CROP_UNALIGNED` and we want to ensure ALIGNED cropping.
   * so flags value is 0. */
  int ret = av_frame_apply_cropping(src, 0);

  if (ret < 0) {
    return ret;
  }
  return 0;
}

/**
 * @brief Scales a frame into a flat 1D matrix buffer targeting a specific pixel format.
 */
static int extract_scaled_matrix (vreader *vr,
                                  AVFrame *frame_in,
                                  uint8_t *matrix,
                                  int matrix_size,
                                  enum AVPixelFormat target_fmt) {

  AVFrame *src = frame_in;
  char *fname = vr->fname;

  enum AVPixelFormat src_format;
  int src_range;
  standardise_pixel_format(src, &src_format, &src_range);

  struct SwsContext *prev_ctx = vr->sws_ctx;

  /* Initialize the Scaler */
  vr->sws_ctx = sws_getCachedContext(vr->sws_ctx, src->width, src->height, src_format, matrix_size,
                                     matrix_size, target_fmt, SWS_AREA, NULL, NULL, NULL);

  if (!vr->sws_ctx) {
    log_error("%s: Failed to allocate SwsContext.", fname);
    return AVERROR(ENOMEM);
  }

  /* Only normalise colourspaces if the pixel format actually uses a YUV matrix */
  bool requires_color_matrix =
      (is_color_matrix_applicable(src_format) && is_color_matrix_applicable(target_fmt)) != 0;

  int ret = 0;
  ret = normalise_sws_colourspace(vr->sws_ctx, src_range);
  if (prev_ctx != vr->sws_ctx && requires_color_matrix && ret != 0) {
    log_error("[%s]: Colourspace normalisation failed: %s", fname, ak_err2str(ret));
    return ret;
  }

  /* Setup destination pointers to write DIRECTLY into flat matrix */
  uint8_t *dst_slices[4] = {matrix, NULL, NULL, NULL};
  int dst_linesizes[4] = {matrix_size, 0, 0, 0};

  ret = sws_scale(vr->sws_ctx, (const uint8_t *const *) src->data, src->linesize, 0, src->height,
                  dst_slices, dst_linesizes);

  if (ret <= 0) {
    log_error("[%s]: Scaling FAILED: `%s`", fname, ak_err2str(ret));
    return ret < 0 ? ret : AVERROR(EINVAL);
  }
  return 0;
}

/**
 * Guarantee an 8-bit luma plane for bar detection.
 *
 * Must run AFTER rotation and BEFORE apply_crop().
 * On success @p frame_out points at a frame whose data[0] is SAFE for detect_black_borders():
 *   1. Either the untouched source (already 8-bit planar luma) - BORROWED,
 *      owned_clone_out is left NULL.
 *   2. Or a clone of the cached grey frame - OWNED by the caller via
 *      owned_clone_out; free it when the iteration is done.
 *
 * Since the cached grey frame is not directly handed out, we are free to mess with the geometry of the
 * frame (e.g. in apply_crop()) without causing corruption of the cached grey-frame.
 *
 * @param frame_out       The frame to process next (always set on success).
 * @param owned_clone_out Receives the owned clone, or stays NULL if the
 *                        result is borrowed. **Non-NULL means "you must free"**.
 */
static int normalise_frame_to_grey8 (vreader *vr, AVFrame **frame_out, AVFrame **owned_clone_out) {

  AVFrame *src = vr->frame;

  if (luma_is_u8_plane0(src->format)) {
    *frame_out = src; /* yuv420p and friends: plane 0 already is what we need */
    return 0;
  }

  /* If we don't have a grey frame yet then allocate one */
  if (!vr->grey_frame) {
    vr->grey_frame = AK_OOM(av_frame_alloc());
  }
  AVFrame *dst = vr->grey_frame;

  /* NOTE: The cached frame is never cropped or scaled in place.
   * So its geometry (width, height, etc) only changes if the DECODER decides to change it.
   * Basically, this path will only run once within a single video file. */
  if (dst->width != src->width || dst->height != src->height) {
    av_frame_unref(dst);
    dst->format = AV_PIX_FMT_GRAY8;
    dst->width = src->width;
    dst->height = src->height;
    if (av_frame_get_buffer(dst, 0) < 0) {
      return AVERROR(ENOMEM);
    }
  }

  enum AVPixelFormat src_fmt;
  int src_range;
  standardise_pixel_format(src, &src_fmt, &src_range);

  vr->grey_sws_ctx = sws_getCachedContext(vr->grey_sws_ctx, src->width, src->height, src_fmt, dst->width,
                                          dst->height, AV_PIX_FMT_GRAY8, SWS_AREA, NULL, NULL, NULL);
  if (!vr->grey_sws_ctx) {
    return AVERROR(ENOMEM);
  }

  int ret = 0;
  ret = sws_scale(vr->grey_sws_ctx, (const uint8_t *const *) src->data, src->linesize, 0, src->height,
                  dst->data, dst->linesize);
  if (ret <= 0) {
    return ret;
  }

  /* copy over frame properties for the `pts` and other important fields */
  ret = av_frame_copy_props(dst, src);
  if (ret != 0) {
    log_warn("[%s] Failed copying frame properties.", vr->fname);
    return AVERROR(ENOMEM);
  }

  /* NOTE: Clone shares the buffer (refcount bump) but owns its crop offsets, so
   * av_frame_apply_cropping() on the clone leaves the cached frame in `vreader` intact. */
  *owned_clone_out = av_frame_clone(dst);
  if (!*owned_clone_out) {
    return AVERROR(ENOMEM);
  }
  *frame_out = *owned_clone_out;
  return 0;
}

/**
 * Initialise a filter graph for rotational transformations.
 *
 * @param fctx Filter context to initialise.
 * @param frame Frame to filter.
 * @param time_base Stream timebase.
 * @param rotation_normalised Normalised rotation, valid values: [90,180,270].
 *
 * @return 0 on success, AV_ERROR on failure.
 */
static AK_NONNULL_ARG(1, 2) int init_rotation_filter_graph (filter_ctx *fctx,
                                                            AVFrame *frame,
                                                            AVRational time_base,
                                                            int rotation_normalised) {

  assert(rotation_normalised == 90 || rotation_normalised == 180 || rotation_normalised == 270);

  char args[512];
  int ret = AK_OK;

  /* Index for filter strings */
  enum FILTER_FOR_ANGLE {
    _90_DEGREES = 0,
    _180_DEGREES = 1,
    _270_DEGREES = 2
  };

  /* Filter strings */
  const char *filter_strings[3] = {[_90_DEGREES] = "transpose=2",
                                   [_180_DEGREES] = "hflip,vflip",
                                   [_270_DEGREES] = "transpose=1"};

  const char *filter_desc = NULL;

  switch (rotation_normalised) {
    case 90:
      filter_desc = filter_strings[_90_DEGREES];
      break;
    case 180:
      filter_desc = filter_strings[_180_DEGREES];
      break;
    case 270:
      filter_desc = filter_strings[_270_DEGREES];
      break;
    default:
      log_error("Cannot handle %d rotation.", rotation_normalised);
      return AVERROR(EINVAL);
  }

  const AVFilter *buffersrc = avfilter_get_by_name("buffer");
  const AVFilter *buffersink = avfilter_get_by_name("buffersink");
  AVFilterInOut *outputs = AK_OOM(avfilter_inout_alloc());
  AVFilterInOut *inputs = AK_OOM(avfilter_inout_alloc());

  fctx->filter_graph = AK_OOM(avfilter_graph_alloc());

  /* Format filter string */
  snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           frame->width, frame->height, frame->format, time_base.num, time_base.den,
           frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);

  ret = avfilter_graph_create_filter(&fctx->buffersrc_ctx, buffersrc, "in", args, NULL, fctx->filter_graph);
  if (ret < 0) {
    goto end;
  }

  AVBufferSrcParameters *par = AK_OOM(av_buffersrc_parameters_alloc());

  par->format = frame->format;
  par->time_base = time_base;
  par->width = frame->width;
  par->height = frame->height;
  par->sample_aspect_ratio = frame->sample_aspect_ratio;
  par->color_space = frame->colorspace;
  par->color_range = frame->color_range;
  ret = av_buffersrc_parameters_set(fctx->buffersrc_ctx, par);
  av_freep((void *) &par); /* Free the allocated struct */
  if (ret != 0) {
    goto end;
  }
  ret = avfilter_graph_create_filter(&fctx->buffersink_ctx, buffersink, "out", NULL, NULL,
                                     fctx->filter_graph);
  if (ret != 0) {
    goto end;
  }

  outputs->name = AK_OOM(av_strdup("in"));
  outputs->filter_ctx = fctx->buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = AK_OOM(av_strdup("out"));
  inputs->filter_ctx = fctx->buffersink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;
  ret = avfilter_graph_parse_ptr(fctx->filter_graph, filter_desc, &inputs, &outputs, NULL);
  if (ret < 0) {
    goto end;
  }

  ret = avfilter_graph_config(fctx->filter_graph, NULL);

end:
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);
  if (ret < 0 && fctx->filter_graph) {
    avfilter_graph_free(&fctx->filter_graph);
    fctx->filter_graph = NULL;
  }
  return ret;
}

AK_DEFINE_AUTO(avframe, AVFrame *, if (ak__obj) av_frame_free(ak__obj))

AK_DEFINE_AUTO(filterctx, filter_ctx, if (ak__obj->init) avfilter_graph_free(&ak__obj->filter_graph));

static AK_ALWAYS_INLINE AK_NONNULL_ARG(1) void mark_segment_failed (ak_hash_entry *entries,
                                                                    ptrdiff_t index) {
  entries[index].hash = 0;
  entries[index].timestamp = 0;
}

/**
 * Open file with libav and hash frames.
 *
 * @param file File to hash.
 * @param config Runtime configuration.
 * @param [out] entries_out Results from hashing are written here.
 *
 * @return AK_STATUS
 */
enum AK_STATUS ak_video_hash (ak_file *file,
                              ak_config *config,
                              ak_signals_ctx *signals,
                              ak_hash_entry *entries_out) {

  assert(file);
  assert(entries_out);
  assert(config->segments > 0);
  AK_ASSUME(config->segments < INT_MAX);

  int target_segments = (int) config->segments;
  int ret = AK_OK;

  vreader vreader AK_AUTO(vreader_close) = {0};

  /* Setup video reader */
  ret = vreader_init(file->path, signals, &vreader);
  if (ret != 0) {
    return (ret == AVERROR(ENOMEM)) ? AK_OOM : AK_IO_FAIL;
  }
  vreader.fname = ak_file_name(file);
  const char *vr_fname = vreader.fname;

  file->duration_us = vreader_get_duration(&vreader);
  double duration_s = ak_time_microsec_sec(file->duration_us);

  /* As long as this is true we won't break anything when we cast for libav */
  AK_ASSUME(file->duration_us < INT64_MAX);

  /*
   * DURATION VALIDATION
   */
  int64_t min_duration_us = ak_time_sec_microsec((double) config->skip_duration);
  /* Return early if duration is 0 */
  if (file->duration_us == 0) {
    log_info("[%s] SKIPPING: Video duration is zero (%" PRId64 ")", vr_fname, file->duration_us);
    return AK_SKIP_SHORT_DURATION;
  }
  if (file->duration_us <= min_duration_us) {
    log_info("[%s] SKIPPING: Duration (%.1f s) less than minimum threshold (%zu s)", vr_fname, duration_s,
             config->skip_duration);
    return AK_SKIP_SHORT_DURATION;
  }

  /* Return early if duration (seconds) is lower than the number of targeted segments */
  if (duration_s < target_segments) {
    log_info("[%s] SKIPPING: Video duration (%.1f s) too short for # of segments (%d)", vr_fname,
             duration_s, target_segments);
    return AK_SKIP_SHORT_DURATION;
  }

  /*
   * HASHING SETUP
   */

  /* Frames to step by for each segment */
  const i64 frame_step_us = (file->duration_us / target_segments);
  /* The seeking jumps by this many microseconds */
  const i64 seek_target_us_jump = (frame_step_us / 2);

  /* Counter for # of frames successfully decoded */
  u32 frames_decoded = 0;
  /* Previously decoded frames PTS, initialise it to -1 */
  int64_t last_pts_streambase = -1;

  /* Stores our matrix of black and white pixels */
  uint8_t matrix[AK_PHASH_TOTAL_PIXELS] = {0};

  /* Video stream */
  AVStream *video_stream = vreader_video_stream(&vreader);
  /* Video streams timebase */
  const AVRational stream_timebase = video_stream->time_base;

  /* Should we detect black bars in the video frame? */
  bool detect_bars = ak_flag_has(config->detect_flags, DETECT_BARS);

  /* Filter context in case we need to run any filters on frames */
  filter_ctx fctx AK_AUTO(filterctx) = {0};
  AVFrame *filtered_frame AK_AUTO(avframe) = NULL;

  /* Check metadata for whether frame should be rotated */
  int rotation_normalised = normalise_angle_360(get_video_stream_rotation(video_stream));
  if (rotation_normalised) {
    log_info("[%s] Detected rotation: %d degrees (normalised)\n", vr_fname, rotation_normalised);
    filtered_frame = AK_OOM(av_frame_alloc());
  }

  /*
   * Main Loop
   */
  for (int i = 0; i < target_segments; i++) {

    /* Find frame with this timestamp */
    int64_t seek_target_us = (int64_t) ((i * frame_step_us) + seek_target_us_jump);
    /* Target timestamp in streams time base (tick) */
    int64_t seek_target_sb = av_rescale_q(seek_target_us, AV_TIME_BASE_Q, stream_timebase);
    double seek_target_seconds = ak_time_microsec_sec(seek_target_us);

    log_trace("[%s] [%d/%d] -> Seeking to PTS `%" PRId64 "` (%.1f s)", vr_fname, (i + 1), target_segments,
              seek_target_sb, seek_target_seconds);

    /* Non-null iff normalise_frame_to_grey8() allocated a clone of a frame for us.
     * Cleaned up every iteration (whether it is null or non-null). */
    AVFrame *owned_greyscale_clone AK_AUTO(avframe) = NULL;

    /*
     * Seek to timestamp
     */
    ret = vreader_seek_decode_to_target(&vreader, seek_target_sb, last_pts_streambase);
    if (ret != AK_OK) {
      if (ret == AVERROR_EXIT) {
        log_warn("[%s] Hashing interrupted by shutdown request.\n", vr_fname);
        return AK_IO_FAIL;
      }
      if (ret == AVERROR(ENOMEM)) {
        return AK_OOM;
      }
      log_warn("[%s] [%d/%d] Failed seeking PTS `%" PRId64 "`(%.1f s): %s", vr_fname, (i + 1),
               target_segments, seek_target_sb, seek_target_seconds, ak_err2str(ret));
      goto segment_failed;
    }

    int64_t pts_streambase = get_frame_pts(vreader.frame);
    int64_t pts_microseconds = pts_to_useconds(pts_streambase, stream_timebase);
    last_pts_streambase = pts_streambase; /* Keep tracked for next iteration */

    if (ak_unlikely(pts_microseconds < 0)) {
      log_error("[%s] ??? Frame timestamp is negative (%ld microsecs), defaulting to 0.", vr_fname,
                pts_microseconds);
      pts_microseconds = 0;
    }

    double pts_seconds = ak_time_microsec_sec(pts_microseconds);

    /*
     * ROTATION HANDLING
     * Set up filter context and filters frames to rotate it
     * TODO: Instead of failing the segment when rotation fails,
     * just try to continue without using the rotation filter graph
     */
    if (rotation_normalised) {

      /* If filter context not initialised, lets initialise it now */
      if (!fctx.init) {
        ret = init_rotation_filter_graph(&fctx, vreader.frame, stream_timebase, rotation_normalised);
        if (ret < 0) {
          log_error("[%s] Failed to init filter graph: %s", vr_fname, ak_err2str(ret));

          if (ret == AVERROR(ENOMEM)) {
            return AK_OOM;
          }
          goto segment_failed;
        }
        fctx.init = 1;
      }

      /* Add frame to filter */
      ret = av_buffersrc_add_frame_flags(fctx.buffersrc_ctx, vreader.frame, AV_BUFFERSRC_FLAG_KEEP_REF);
      if (ret < 0) {
        log_error("[%s] Failed add frame to filter graph: %s", vr_fname, ak_err2str(ret));
        goto segment_failed;
      }

      /* Retrieve filtered frame */
      ret = av_buffersink_get_frame(fctx.buffersink_ctx, filtered_frame);
      if (ret < 0) {
        log_error("[%s] Failed retrieve frame from filter graph: %s", vr_fname, ak_err2str(ret));
        goto segment_failed;
      }

      /* Swap original frame out with the new filtered one. */
      av_frame_unref(vreader.frame);
      av_frame_move_ref(vreader.frame, filtered_frame);
    }

    /* `proc` is the 'working frame' for the rest of the iteration.
     * Every stage that follows will operate on `proc` without care for the frame that backs it.
     * It is a 'borrow', not an 'owner':
     *   - Starts as an alias to the frame stored in vreader.
     *   - `normalise_frame_to_grey8()` may re-point to a greyscale clone.
     * Aliasing is important as it never messes with the vreaders own pointer, and
     * downstream code doesn't need "which frame is current?" branching logic:
     * For example:
     *   - detect_bars off               -> proc stays `vreader.frame`
     *   - detect_bars on, no conversion -> proc stays `vreader.frame`
     *   - detect_bars on, converted     -> proc points at the clone (`owned_greyscale_clone`)
     */
    AVFrame *proc = vreader.frame;
    /*
     * BAR DETECTION
     */
    if (detect_bars) {

      /* Normalise frame to greyscale */
      ret = normalise_frame_to_grey8(&vreader, &proc, &owned_greyscale_clone);

      if (ret != 0) {
        log_error("[%s] Could not greyscale frame (%" PRIi64 " us): %s", vr_fname, pts_microseconds,
                  ak_err2str(ret));
        if (ret == AVERROR(ENOMEM)) {
          return AK_OOM;
        }

        goto segment_failed;
      }
      /* Apply cropping:
       * TODO If frame is too dark, we should try to decode another frame
       * If all frames are dark (we can set some limit to the # of black frames),
       * then we should fail for the file and signal this to our caller.
       */
      ret = apply_crop(&vreader, proc, AK_DEFAULT_BLACK_THRESHOLD, 0);
      if (ret < 0) {
        log_error("[%s] Cropping failed (%" PRIi64 " us): %s", vr_fname, pts_microseconds, ak_err2str(ret));
        if (ret == AVERROR(ENOMEM)) {
          return AK_OOM;
        }
        goto segment_failed;
      }
    }

    /* SCALING:
     * Scale frame to 32x32 (whilst converting to GRAY8 if it is necessary).
     * Extracts out the pixel buffer from the frame and places it in `matrix`.
     */
    ret = extract_scaled_matrix(&vreader, proc, matrix, AK_PHASH_INPUT_SIZE, AV_PIX_FMT_GRAY8);
    if (ret) {
      log_error("[%s] Failed to scale frame %s (%.1f s):", vr_fname, ak_err2str(ret), pts_seconds);
      if (ret == AVERROR(ENOMEM)) {
        return AK_OOM;
      }
      goto segment_failed;
    }

    /*
     * HASH FRAME AND STORE DATA INTO SEGMENT
     * - Hash the matrix
     * - Store timestamp of frame that was hashed
     */

    ak_hash_entry *segment = (entries_out + i);
    segment->hash = hash_decoded_frame(matrix, config->hash_algorithm);
    segment->timestamp = pts_microseconds;

    log_trace("[%s] [%d/%d] -> PTS %" PRId64 " (%.1f s)  produced hash `%" PRIX64 "`", vr_fname, (i + 1),
              target_segments, pts_microseconds, pts_seconds, entries_out[i].hash);

    frames_decoded++;

    /* Skip error block */
    continue;

  segment_failed: /* Failure to hash segment */
    {
      mark_segment_failed(entries_out, i);
    }
  }

  if (frames_decoded == 0) {
    log_debug("[%s] Failed to hash a single frame.", vr_fname);
    return AK_IO_FAIL;
  }

  log_trace("[%s] DONE. Processed %d frames.", vr_fname, frames_decoded);
  return AK_OK;
}
