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

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "hash.h"
#include "log.h"
#include "util.h"

typedef struct cropping {
  int x;
  int y;
  int w;
  int h;
} cropping;

typedef struct filter_ctx {
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
  int init;
} filter_ctx;

/**
 * Destructor for vreader.
 *
 * @param [in] vreader vreader to destroy.
 */
static void vreader_close (anu_vreader *vreader) {
  if (!vreader) {
    return;
  }
  av_packet_free(&vreader->packet);
  sws_freeContext(vreader->sws_ctx);
  av_frame_free(&vreader->frame);
  avcodec_free_context(&vreader->codec_ctx);
  avformat_close_input(&vreader->fmt_ctx);
}

/**
 * Auto-cleanup helper for vreader.
 */
DEFINE_FREE(vreader_close, anu_vreader, vreader_close(&_T))

/**
 * Helper function to retreive video stream from an initialised vreader.
 *
 * @return Pointer to video stream (AVStream).
 */
static ALWAYS_INLINE _nonnull_ (1) AVStream *vreader_video_stream(anu_vreader *vreader) {
  return vreader->fmt_ctx->streams[vreader->video_stream_idx];
}

/**
 * Helper function to return the file URL from an initialised vreader.
 *
 * @return The URL of the file as a char pointer.
 */
static ALWAYS_INLINE _nonnull_ (1) char *vreader_fmt_url(anu_vreader *vreader) {
  return vreader->fmt_ctx->url;
}

/**
 * Convert a PTS from a specified timebase to MICROSECONDS.
 *
 * @param pts PTS value.
 * @param timebase Timebase that PTS is currently using.
 * @return PTS value in microseconds (useconds) or AV_NOPTS_VALUE if pts is invalid.
 */
static ALWAYS_INLINE _const_ int64_t pts_to_useconds (int64_t pts, AVRational timebase) {
  return (pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

/**
 * Convert a PTS from a specified timebase to SECONDS.
 *
 * @param pts PTS value.
 * @param timebase Timebase that PTS is currently using.
 * @return PTS value in seconds or AV_NOPTS_VALUE if pts is invalid.
 */
static ALWAYS_INLINE _const_ double pts_to_seconds (int64_t pts, AVRational timebase) {
  return (pts == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : (double) pts * av_q2d(timebase);
}

/**
 * Retrieve a sane PTS value from frame.
 *
 * @param [in]frame Frame to retrieve PTS for.
 * @return The PTS in the streams timebase OR if pts is not available,
 * then the frames best effort timestamp (also in stream timebase).
 */
static ALWAYS_INLINE _pure_ int64_t get_frame_pts (const AVFrame *frame) {
  return (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->best_effort_timestamp;
}

/**
 * Normalise an angle to between 0 and 360 degrees.
 *
 * @param angle Input angle (in degrees).
 * @return Normalised angle (degrees).
 */
static ALWAYS_INLINE _const_ int normalise_angle_360 (const int angle) {
  return (((angle % 360) + 360) % 360);
}

/**
 * Check metadata of stream for display transformations (rotations).
 *
 * @return Rotation angle between -180 and 180 degrees (if found).
 * @retval 0 if no rotation data.
 */
static _nonnull_(1) int get_video_stream_rotation(anu_vreader *vr) {
  /* Search the side data array inside the codec parameters */
  AVStream *stream = vreader_video_stream(vr);
  const AVPacketSideData *sd = av_packet_side_data_get(
      stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);

  if (!sd) {
    return 0;
  }

  int32_t *display_matrix = (int32_t *) sd->data;
  int rotation = (int) av_display_rotation_get(display_matrix);

  return rotation;
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
 * @return ANU_OK if success, anything else is an error.
 *
 */
static _nonnull_(1, 2) ANU_STATUS vreader_init(const char *f_path, anu_vreader *vreader) {

  /* Assign video stream index to an invalid index by default */
  vreader->video_stream_idx = -1;

  int errcode = 0;

  /*
   * Initialise FORMAT CONTEXT.
   * This step will check if file is existent, can be opened, etc.
   */

  /* Opens input file and guesses format of file */
  errcode = avformat_open_input(&vreader->fmt_ctx, f_path, NULL, NULL);

  if (errcode != 0) {
    log_error("[%s] Could not open file. (%s)", f_path, av_err2str(errcode));
    return ANU_LIBAV_FAIL;
  }

  /*
   * Read bytes from file / decode a few frames to fill out context that the
     method above missed.
   * `avformat_open_input` will only read header of file (which may not always be accurate).
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    log_error("[%s] Failed to read both file header and stream info. (%s)", f_path, av_err2str(errcode));
    return ANU_LIBAV_FAIL;
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
      log_error("[%s] Failed to find best stream: %s", f_path, av_err2str(vreader->video_stream_idx));
    }

    return ANU_LIBAV_FAIL;
  }

  if (!codec) {
    log_error("[%s] No codec found for stream.", f_path);
    return ANU_LIBAV_FAIL;
  }

  AVStream *vid_stream = vreader_video_stream(vreader);

  log_trace("[%s] Video stream idx: [%d]", f_path, vreader->video_stream_idx);

  /* Discard ALL non-video streams */
  for (unsigned int i = 0; i < vreader->fmt_ctx->nb_streams; i++) {
    if (i != (unsigned int) vreader->video_stream_idx) {
      vreader->fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    }
  }

  /* Allocate Codec Context */
  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);

  if (codec_ctx == NULL) {
    log_error("[%s] Failed to allocate memory for codec context.", f_path);
    return ANU_OOM;
  }

  /* Get codec parameters required for video stream */
  AVCodecParameters *codec_params = vid_stream->codecpar;
  errcode = avcodec_parameters_to_context(codec_ctx, codec_params);
  if (errcode < 0) {
    log_error("[%s] Could not initialise codec with supplied parameters (%s)", f_path, av_err2str(errcode));
    return ANU_LIBAV_FAIL;
  }

  /*
   * Settings for our decoder
   */

  /* NOTE: Set thread count to prevent CACHE THRASHING */
  codec_ctx->thread_count = 1;
  /* Disable applying filter to save processing power */
  codec_ctx->skip_loop_filter = AVDISCARD_ALL;
  /* Decode videos in grayscale */
  codec_ctx->flags |= AV_CODEC_FLAG_GRAY;
  /* Skip frames that are not reference frames */
  codec_ctx->skip_frame = AVDISCARD_NONREF;

  /* Initialise the codec context for use with our codec */
  errcode = avcodec_open2(codec_ctx, codec, NULL);
  if (errcode < 0) {
    log_error("[%s] Failed to initialise codec context %s (%s)", f_path, codec->long_name,
              av_err2str(errcode));
    return ANU_LIBAV_FAIL;
  }
  vreader->codec_ctx = codec_ctx;

  /* Alloc Buffers */
  vreader->frame = av_frame_alloc();
  vreader->packet = av_packet_alloc();

  if (!vreader->frame || !vreader->packet) {
    log_error("[%s] Failed to allocate memory for frame/packet.", f_path);
    return ANU_OOM;
  }

  return ANU_OK;
}

/**
 * @brief Get duration of video in microseconds.
 *
 * Retrieves duration of video either by using the video stream or falling back to container.
 *
 * @return Duration of video in microseconds.
 *
 */
static i64 vreader_get_duration (anu_vreader *vreader) {

  AVStream *vid_stream = vreader_video_stream(vreader);

  /* duration in stream-base */
  int64_t duration = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;

  /* If duration is without a value then we get the container provided duration */
  if (duration == AV_NOPTS_VALUE) {

    /* NOTE: Container durations are in microseconds (AV_TIME_BASE) */
    duration = (vreader->fmt_ctx->duration) > 0 ? vreader->fmt_ctx->duration : 0;
    log_debug(
        "[%s] Video stream omitting duration, using container values as "
        "fallback (%.2fs)",
        vreader->fname, anu_time_microseconds_to_seconds(duration));
    return duration;
  }

  /* If duration is larger than 0 then convert stream timebase duration to microseconds (AV_TIME_BASE) */
  return duration > 0 ? pts_to_useconds(duration, stream_timebase) : 0;
}

/**
 * @brief Seek to timestamp.
 *
 * Seeks to nearest preceding keyframe from target timestamp.
 *
 * @param vreader VideoReader instance.
 * @param target_pts_streambase Target time stamp (in streams own time base).
 * @return 0 on success, anything else on failure.
 *
 * @note When `av_seek_frame` fails, this function returns libav's err code.
 */
static int vreader_seek_pts (anu_vreader *vreader, int64_t target_pts_streambase) {

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking. */
  int seek_ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx, target_pts_streambase,
                               AVSEEK_FLAG_BACKWARD);

  if (seek_ret < 0) {
    return seek_ret;
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
 * @retval ANU_OK Successfully decoded packet.
 * @retval -1 End of file.
 * @retval -11 Error, please try again.
 * @retval Anything else is an unknown error.
 */
static int vreader_decode_frame (anu_vreader *vreader) {
  int ret;
  AVCodecContext *codec_ctx = vreader->codec_ctx;

  for (;;) {
    /* Try to grab a decoded frame first */
    ret = avcodec_receive_frame(codec_ctx, vreader->frame);

    if (ret >= 0) {
      /* Success: We have a frame */
      return ANU_OK;
    }
    if (ret == AVERROR_EOF) {
      /* EOF reached */
      return ret;
    }
    if (ret != AVERROR(EAGAIN)) {
      /* Fatal decoding error */
      log_error("[%s] Error receiving frame: %s", vreader->fname, av_err2str(ret));
      return ret;
    }

    /* EAGAIN means the decoder needs more data. Read a packet. */
    ret = av_read_frame(vreader->fmt_ctx, vreader->packet);
    if (ret == AVERROR_EOF) {
      /* Flush the decoder and loop back to receive the remaining frames */
      avcodec_send_packet(codec_ctx, NULL);
      continue;
    }

    if (ret < 0) {
      log_error("[%s] Error reading packet: %s", vreader->fname, av_err2str(ret));
      return ret;
    }

    /* Send the correct video packet to the decoder */
    ret = avcodec_send_packet(codec_ctx, vreader->packet);
    av_packet_unref(vreader->packet);

    if (ret < 0) {
      log_error("%s Decoding error: %s", vreader->fname, av_err2str(ret));
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
 * @return ANU_OK if success, AV_ERR on failure.
 *
 */
static int vreader_seek_decode_to_target (anu_vreader *vreader,
                                          int64_t target_pts_streambase,
                                          int64_t min_pts_streambase) {

  int ret = vreader_seek_pts(vreader, target_pts_streambase);
  if (ret != 0) {
    return ret;
  }

  for (;;) {
    ret = vreader_decode_frame(vreader);
    if (ret != ANU_OK) {
      return ret; /* EOF or decoding error */
    }

    int64_t current_pts_sb = get_frame_pts(vreader->frame);

    /* Check if we reach desired target pts OR we reach a frame higher than minimum pts */
    if ((current_pts_sb >= target_pts_streambase) && (current_pts_sb > min_pts_streambase)) {
      return ANU_OK;
    }
  }
}

/**
 * Helper to check if a row contains non-black pixels.
 * @param row An array of pixels in the row.
 * @param width Width of row (e.g. length of row array).
 * @param threshold Pixel value must be above this threshold to return true.
 *
 * @return bool Whether there is a pixel in that row that has a pixel value above the threshold.
 */
static ALWAYS_INLINE _pure_ _nonnull_ (1) bool row_has_video(const uint8_t *const restrict row,
                                                             const int width,
                                                             const int threshold) {
  ANU_ASSUME(width >= 0 && threshold > 0);

  for (int i = 0; i < width; i++) {
    if (row[i] > threshold) {
      return true;
    }
  }
  return false;
}

/* Detects the bounding box of non-black pixels */
static ALWAYS_INLINE bool detect_black_borders (AVFrame *frame, const int threshold, cropping *crop_out) {

  const int w = frame->width;
  const int h = frame->height;
  const ptrdiff_t linesize = frame->linesize[0];
  const uint8_t *const y_plane = frame->data[0];

  int top = 0;
  int bottom = h - 1;

  const uint8_t *row_ptr = y_plane;

  while ((top < h) /* Top bound */
         && !(row_has_video(row_ptr, w, threshold))) {
    ++top;
    row_ptr += linesize;
  }

  /* Return false if frame is completely black */
  if (top == h) {
    return false;
  }

  const uint8_t *bottom_ptr = y_plane + (bottom * linesize);

  /* Find bottom bound */
  while ((bottom > top) && !(row_has_video(bottom_ptr, w, threshold))) {
    --bottom;
    bottom_ptr -= linesize;
  }

  int left = w - 1;
  int right = 0;

  /* Reset row pointer */
  row_ptr = y_plane + (top * linesize);

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

  crop_out->x = left;
  crop_out->y = top;
  ANU_ASSUME(((right - left) + 1) > 0);
  crop_out->w = (right - left) + 1;
  ANU_ASSUME(((bottom - top) + 1) > 0);
  crop_out->h = (bottom - top) + 1;
  return true;
}

/**
 * @brief Produce hash from a video frame.
 * @param matrix 1D array of pixel values.
 * @param hash_algo TODO The type of hashing algorithm to use. Currently does not do anything.
 * @return Unsigned 64 bit integer (hash).
 */
static _pure_ uint64_t hash_decoded_frame (const uint8_t *restrict matrix, const anu_hash_type hash_algo) {

  if (hash_algo != ANU_HASH_ALGO_DCT) {
    ANU_TODO("We've only implemented DCT hashing thus far.");
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
 * @retval -1 Failure to get or set the software scaler's colourspace.
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

  /* Get default values */
  if (sws_getColorspaceDetails(context, &inv_table, &curr_src, &table, &curr_dst, &brightness, &contrast,
                               &saturation) < 0) {
    log_error("Failed to get colorspace details.");
    return -1;
  }

  /* Return early if source and dest ranges are the same */
  if (curr_src == src_range && curr_dst == dst_range) {
    return 0;
  }

  /* Apply explicit ranges. */
  if (sws_setColorspaceDetails(context, inv_table, src_range, table, dst_range, brightness, contrast,
                               saturation) < 0) {
    log_error("Failed to set colourspace.");
    return -1;
  }
  return 0;
}

/**
 * @brief Checks if an AVPixelFormat is Grayscale or RGB.
 */
static bool is_color_matrix_applicable (enum AVPixelFormat fmt) {
  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
  if (!desc) {
    return false;
  }

  /* If it's RGB or has less than 3 components (like GRAY8), YUV matrices don't apply */
  if ((ANU_HAS_ANY_FLAG(desc->flags, AV_PIX_FMT_FLAG_RGB)) || (desc->nb_components < 3)) {
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
static void standardise_pixel_format (const AVFrame *src,
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
 * @brief Detects black borders and applies cropping to the AVFrame.
 */
static int apply_crop (anu_vreader *vr, int threshold_black, int threshold_white) {

  AVFrame *src = vr->frame;
  cropping crop = {.x = 0, .y = 0, .w = src->width, .h = src->height};

  const int threshold = threshold_black ? threshold_black : 24;
  /* 24 is usually a safe threshold for limited-range YUV "black" */
  if (!detect_black_borders(src, threshold, &crop)) {
    log_warn("[%s]: Frame is completely black.", vr->fname);
    return ANU_FRAME_BLACK;
  }

  const int c_left = crop.x;
  const int c_top = crop.y;
  const int c_right = (src->width - crop.w - crop.x);
  const int c_bottom = (src->height - crop.h - crop.y);

  if (c_left || c_top || c_right || c_bottom) {
    log_info("[%s]: Cropping frame (%f s) from (%d,%d) to: (width=[%d-%d], height=[%d-%d])", vr->fname,
             pts_to_seconds(get_frame_pts(src), vreader_video_stream(vr)->time_base), src->width,
             src->height, crop.x, crop.w, crop.y, crop.h);
  }

  src->crop_left = (size_t) c_left;
  src->crop_top = (size_t) c_top;
  src->crop_right = (size_t) c_right;
  src->crop_bottom = (size_t) c_bottom;

  /*
   * NOTE: The only flag currently is AV_FRAME_CROP_UNALIGNED and we want ALIGNED cropping
   */
  const int crop_flags = 0;

  int ret = av_frame_apply_cropping(src, crop_flags);
  if (ret < 0) {
    log_error("[%s]: Failed to apply cropping: %s", vr->fname, av_err2str(ret));
    return ANU_LIBAV_FAIL;
  }
  return 0;
}

/**
 * @brief Scales a frame into a flat 1D matrix buffer targeting a specific pixel format.
 */
static int extract_scaled_matrix (anu_vreader *vr,
                                  uint8_t *matrix,
                                  int matrix_size,
                                  enum AVPixelFormat target_fmt) {
  AVFrame *src = vr->frame;
  char *fname = vr->fname;

  enum AVPixelFormat src_format;
  int src_range;
  standardise_pixel_format(src, &src_format, &src_range);

  struct SwsContext *prev_ctx = vr->sws_ctx;

  /* Initialize the Scaler */
  vr->sws_ctx = sws_getCachedContext(vr->sws_ctx, src->width, src->height, src_format, matrix_size,
                                     matrix_size, target_fmt, SWS_AREA, NULL, NULL, NULL);

  if (!vr->sws_ctx) {
    log_error("%s: Failed to create scaling context.", fname);
    return ANU_LIBAV_FAIL;
  }

  /* Only normalise colourspaces if the pixel format actually uses a YUV matrix */
  bool requires_color_matrix =
      (is_color_matrix_applicable(src_format) && is_color_matrix_applicable(target_fmt)) != 0;

  if (prev_ctx != vr->sws_ctx && requires_color_matrix) {
    if (normalise_sws_colourspace(vr->sws_ctx, src_range)) {
      log_error("[%s]: Colourspace normalisation failed.", fname);
      return ANU_LIBAV_FAIL;
    }
  }

  /* Setup destination pointers to write DIRECTLY into flat matrix */
  uint8_t *dst_slices[4] = {matrix, NULL, NULL, NULL};
  int dst_linesizes[4] = {matrix_size, 0, 0, 0};

  int scaling_ret = sws_scale(vr->sws_ctx, (const uint8_t *const *) src->data, src->linesize, 0,
                              src->height, dst_slices, dst_linesizes);

  if (scaling_ret <= 0) {
    log_error("%s: Scaling FAILED: `%s`", fname, av_err2str(scaling_ret));
    return ANU_LIBAV_FAIL;
  }

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
 * @return ANU_OK on success, AV_ERROR on failure.
 */
static _nonnull_(1, 2) int init_rotation_filter_graph(filter_ctx *fctx,
                                                      AVFrame *frame,
                                                      AVRational time_base,
                                                      int rotation_normalised) {

  assert(rotation_normalised == 90 || rotation_normalised == 180 || rotation_normalised == 270);

  char args[512];
  int ret = ANU_OK;

  /* Index for filter strings */
  enum FILTER_FOR_ANGLE { _90_DEGREES = 0, _180_DEGREES = 1, _270_DEGREES = 2 };

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
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs = avfilter_inout_alloc();

  fctx->filter_graph = avfilter_graph_alloc();
  if (!outputs || !inputs || !fctx->filter_graph) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  /* Format filter string */
  snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           frame->width, frame->height, frame->format, time_base.num, time_base.den,
           frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);

  ret = avfilter_graph_create_filter(&fctx->buffersrc_ctx, buffersrc, "in", args, NULL, fctx->filter_graph);
  if (ret < 0) {
    goto end;
  }

  AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
  if (par) {
    par->format = frame->format;
    par->time_base = time_base;
    par->width = frame->width;
    par->height = frame->height;
    par->sample_aspect_ratio = frame->sample_aspect_ratio;
    par->color_space = frame->colorspace;
    par->color_range = frame->color_range;

    av_buffersrc_parameters_set(fctx->buffersrc_ctx, par);
    av_freep((void *) &par); /* Free the allocated struct */
  }

  ret = avfilter_graph_create_filter(&fctx->buffersink_ctx, buffersink, "out", NULL, NULL,
                                     fctx->filter_graph);
  if (ret < 0) {
    goto end;
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = fctx->buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
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
  }
  return ret;
}

static ALWAYS_INLINE _nonnull_ (1) void mark_segment_failed(hash_entry *entries, ptrdiff_t index) {
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
 * @return ANU_STATUS
 */
enum ANU_STATUS anu_video_hash (anu_file *file, anu_config *config, hash_entry *entries_out) {

  assert(file);
  assert(entries_out);

  assert(config->segments > 0);
  ANU_ASSUME(config->segments < INT_MAX);
  int target_segments = (int) config->segments;

  anu_vreader vreader __free(vreader_close) = {0};

  /* Setup video reader */
  int code = 0;
  code = vreader_init(file->path, &vreader);
  if (code != ANU_OK) {
    return code;
  }
  vreader.fname = anu_file_get_filename(file);
  char *vr_fname = vreader.fname;

  file->duration_us = vreader_get_duration(&vreader);

  /* Return early if duration is 0 */
  if (file->duration_us == 0) {
    log_info("[%s] SKIPPING: Video duration is zero (%zu)", vr_fname, file->duration_us);
    return ANU_SKIPPED_SHORT_DURATION;
  }

  if (file->duration_us < target_segments) {
    log_info("[%s] SKIPPING: Video duration (%zu s) too short for # of segments (%d)", vr_fname,
             file->duration_us, target_segments);
    return ANU_SKIPPED_SHORT_DURATION;
  };

  /* As long as this is true we won't break anything when we cast for libav */
  assert(file->duration_us < INT64_MAX);
  ANU_ASSUME(file->duration_us < INT64_MAX);

  /* Check if file duration is longer than the skip threshold */
  if (file->duration_us <= (anu_time_seconds_to_microseconds((double) config->skip_duration))) {
    log_info("[%s] SKIPPING: Duration (%.1f s) less than minimum threshold (%zu s)", vr_fname,
             anu_time_microseconds_to_seconds(file->duration_us), config->skip_duration);

    return ANU_SKIPPED_SHORT_DURATION;
  }

  const i64 frame_step_us = (file->duration_us / target_segments);
  /* Counter for # of frames successfully decoded */
  int frames_decoded = 0;

  /* Target timestamp in microseconds */
  int64_t seek_target_us = 0;
  const i64 seek_target_us_jump = (frame_step_us / 2);

  uint8_t matrix[ANU_PHASH_TOTAL_PIXELS] = {0};

  /* Video stream */
  AVStream *video_stream = vreader_video_stream(&vreader);

  const AVRational stream_timebase = video_stream->time_base;
  /* Previously decoded frames PTS */
  int64_t last_pts_streambase = -1;

  /* Filter context in case we need to run any filters on frames */
  filter_ctx fctx = {0};
  /* Filtered frame */
  AVFrame *filtered_frame = NULL;

  /* Check for whether stream should be rotated (this is a metadata check) */
  int rotation = get_video_stream_rotation(&vreader);
  int rotation_normalised = normalise_angle_360(rotation);
  if (rotation_normalised) {
    log_info("[%s] Detected rotation: %d degrees (%d degrees normalised)\n", vr_fname, rotation,
             rotation_normalised);
    filtered_frame = av_frame_alloc();
  }
  bool detect_bars = ANU_HAS_ANY_FLAG(config->detect_flags, DETECT_BARS);

  /*
   * Main Loop
   */
  for (int i = 0; i < target_segments; i++) {
    /* Target to seek to in microseconds */
    seek_target_us = (int64_t) ((i * frame_step_us) + seek_target_us_jump);

    /* Target timestamp in streams time base (tick) */
    int64_t seek_target_sb = av_rescale_q(seek_target_us, AV_TIME_BASE_Q, stream_timebase);
    double seek_target_seconds = anu_time_microseconds_to_seconds(seek_target_us);

    int errcode = 0;

    log_trace("[%s] [%d/%d] -> Seeking to PTS `%" PRId64 "` (%.1f s)", vr_fname, (i + 1), target_segments,
              seek_target_sb, seek_target_seconds);

    /* Seek to timestamp */
    errcode = vreader_seek_decode_to_target(&vreader, seek_target_sb, last_pts_streambase);
    if (errcode != ANU_OK) {
      log_error("[%s] [%d/%d] Failed seeking PTS `% " PRId64 "`(%.1f s): %s", vr_fname, (i + 1),
                target_segments, seek_target_sb, seek_target_seconds, av_err2str(errcode));
      mark_segment_failed(entries_out, i);
      continue;
    }

    int64_t pts_streambase = get_frame_pts(vreader.frame);
    int64_t pts_microseconds = pts_to_useconds(pts_streambase, stream_timebase);

    if (UNLIKELY(pts_microseconds < 0)) {
      log_warn(
          "[%s] ??? Frame timestamp is negative (%ld microsecs), defaulting to "
          "0.",
          vr_fname, pts_microseconds);
      pts_microseconds = 0;
    }

    double pts_seconds = anu_time_microseconds_to_seconds(pts_microseconds);

    /* Keep track of frame PTS so we can seek to a higher one next iteration */
    last_pts_streambase = pts_streambase;

    /* If there is a rotation required, then do it now: */
    if (rotation_normalised) {

      /* If filter context not initialised, lets initialise it now */
      if (!fctx.init) {
        int ret = init_rotation_filter_graph(&fctx, vreader.frame, stream_timebase, rotation_normalised);
        if (ret < 0) {
          log_error("[%s] Failed to init filter graph: %s", vr_fname, av_err2str(ret));
          mark_segment_failed(entries_out, i);
          continue;
        }
        fctx.init = 1;
      }

      /* Add frame to filter */
      errcode = av_buffersrc_add_frame_flags(fctx.buffersrc_ctx, vreader.frame, AV_BUFFERSRC_FLAG_KEEP_REF);
      if (errcode < 0) {
        log_error("[%s] Failed add frame to filter graph: %s", vr_fname, av_err2str(errcode));
        mark_segment_failed(entries_out, i);
        continue;
      }

      /* Retrieve filtered frame */
      errcode = av_buffersink_get_frame(fctx.buffersink_ctx, filtered_frame);
      if (errcode < 0) {
        log_error("[%s] Failed retrieve frame from filter graph: %s", vr_fname, av_err2str(errcode));
        mark_segment_failed(entries_out, i);
        continue;
      }

      /* Swap original frame out with the new filtered one. */
      av_frame_unref(vreader.frame);
      av_frame_move_ref(vreader.frame, filtered_frame);
    }

    if (detect_bars) {
      errcode = apply_crop(&vreader, 24, 0);
      if (errcode != 0) {
        log_error("[%s] Cropping failed (%.1f s): %s ", vr_fname, pts_seconds,
                  ((errcode == ANU_FRAME_BLACK) ? "Frame was TOO DARK." : av_err2str(errcode)));
        mark_segment_failed(entries_out, i);
        continue;
      }
    }

    /* Scale down frame to 32x32 (whilst converting to GRAY8 if necessary) */
    errcode = extract_scaled_matrix(&vreader, matrix, ANU_PHASH_INPUT_SIZE, AV_PIX_FMT_GRAY8);
    if (errcode != ANU_OK) {
      log_error("[%s] Failed to scale frame %s (%.1f s):", vr_fname, av_err2str(errcode), pts_seconds);
      mark_segment_failed(entries_out, i);
      continue;
    }

    /*
     * If everything was SUCCESSFUL
     */
    entries_out[i].hash = hash_decoded_frame(matrix, config->hash_algorithm);
    ANU_ASSUME(pts_microseconds >= 0);
    entries_out[i].timestamp = pts_microseconds;
    log_debug("[%s] [%d/%d ] -> PTS %" PRId64 " (%.1f s)  produced hash `%" PRIX64 "`", vr_fname, (i + 1),
              target_segments, pts_microseconds, pts_seconds, entries_out[i].hash);
    ++frames_decoded;
  }

  if (filtered_frame) {
    av_frame_free(&filtered_frame);
  }
  if (fctx.init) {
    avfilter_graph_free(&fctx.filter_graph);
  }

  log_trace("[%s] DONE. Processed %d frames.", vr_fname, frames_decoded);
  return ANU_OK;
}
