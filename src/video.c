#include "video.h"

#include <assert.h>
#include <errno.h> /* IWYU pragma: keep */
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_par.h>
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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int video_reader_get_frame(anu_vreader *vreader);

/**
 * Normalise an angle in degrees to one between 0 and 360.
 *
 * @param angle
 *
 * @return Normalised angle in degrees.
 */
static inline int normalise_angle_360 (int angle) {
  return (((angle % 360) + 360) % 360);
}

static inline int get_video_stream_rotation (anu_vreader *vr) {
  /* Search the side data array inside the codec parameters */
  AVStream *stream = vr->fmt_ctx->streams[vr->video_stream_idx];
  const AVPacketSideData *sd = av_packet_side_data_get(
      stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data,
      AV_PKT_DATA_DISPLAYMATRIX);

  if (!sd) {
    return 0;
  }

  int32_t *display_matrix = (int32_t *) sd->data;
  int rotation = (int) av_display_rotation_get(display_matrix);

  return rotation;
}

static ALWAYS_INLINE _pure_ size_t pts_to_useconds (int64_t pts,
                                                    AVRational timebase) {
  assert(pts >= 0);
  return (size_t) av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

_unused_ static ALWAYS_INLINE _pure_ double frame_pts_to_seconds (
    int64_t pts,
    AVRational timebase) {
  assert(pts >= 0);
  return ((double) av_rescale_q(pts, timebase, AV_TIME_BASE_Q) /
          ANU_TIME_ONE_SEC_IN_US);
}

/**
 * @brief Open video and initialise video struct.
 *
 * This will open a video given by the param 'filename'.
 *
 * You need to call the complimentary function to close and destroy the struct
 * once you are done with it.
 *
 * @param[in] f_path File path.
 * @param[in][out] vreader Structure to initialise.
 * @return ANU_OK if success, anything else is an error.
 *
 */
static enum ANU_STATUS vreader_init (char *f_path, anu_vreader *vreader) {
  assert(f_path && vreader);

  /* Assign video stream index to invalid index by default */
  vreader->video_stream_idx = -1;

  int errcode = 0;
  /*
   * Initialise FORMAT CONTEXT.
   * This step will check if file is existent, can be opened, etc.
   */
  /* Opens input file and guesses format of file */
  errcode = avformat_open_input(&vreader->fmt_ctx, f_path, NULL, NULL);

  if (errcode != 0) {
    log_warn("Could not open file `%s` (%s)", f_path, av_err2str(errcode));
    return ANU_LIBAV_FAIL;
  }

  /*
   * Read bytes from file / decode a few frames to fill out context that the
     method above missed.
   * `avformat_open_input` will only read header of file (which may not always be accurate).
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    log_error("[%s] Failed to read both file header and stream info: `%s`",
              f_path, av_err2str(errcode));
    return ANU_LIBAV_FAIL;
  }

  /*
   * FIND VIDEO STREAM AND DECODER FOR IT
   * Find video stream stored in file.
   * Stores decoder for that video stream in `codec`.
   * Return value of `av_find_best_stream` is the stream index that we store in our struct.
   */
  const AVCodec *codec = NULL;

  vreader->video_stream_idx = av_find_best_stream(
      vreader->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, -1);

  if (vreader->video_stream_idx < 0) {
    if (vreader->video_stream_idx == AVERROR_DECODER_NOT_FOUND) {
      log_error("[%s] No decoder found for stream.", f_path);
    } else if (vreader->video_stream_idx == AVERROR_STREAM_NOT_FOUND) {
      log_error("[%s] No video stream found.", f_path);
    } else {
      log_error("[%s] Failed to find best stream: %s", f_path,
                av_err2str(vreader->video_stream_idx));
    }

    return ANU_LIBAV_FAIL;
  }

  log_trace("[%s] Found video stream at index `%d`", f_path,
            vreader->video_stream_idx);

  if (!codec) {
    log_error("[%s] No codec found for stream.", f_path);
    return ANU_LIBAV_FAIL;
  }

  AVCodecParameters *codec_params = NULL;
  /* Get codec parameters */
  codec_params = vreader->fmt_ctx->streams[vreader->video_stream_idx]->codecpar;

  /* Init Codec Context */
  vreader->codec_ctx = avcodec_alloc_context3(codec);

  if (!vreader->codec_ctx) {
    log_error("[%s] Failed to allocate memory for codec context.", f_path);
    return ANU_OOM;
  }

  if (avcodec_parameters_to_context(vreader->codec_ctx, codec_params) < 0) {
    log_error("[%s] Could not retrieve codec context.", f_path);
    return ANU_LIBAV_FAIL;
  }

  /* NOTE: Set thread count to prevent CACHE THRASHING */
  vreader->codec_ctx->thread_count = 1;
  if (avcodec_open2(vreader->codec_ctx, codec, NULL) < 0) {
    log_error("[%s] Failed to initialise codec context %s", f_path,
              codec->long_name);
    return ANU_LIBAV_FAIL;
  }

  /* Alloc Buffers */
  vreader->frame = av_frame_alloc();

  if (vreader->frame == NULL) {
    log_error("[%s] Failed to allocate memory for frame.", f_path);
    return ANU_OOM;
  }

  vreader->packet = av_packet_alloc();
  if (vreader->packet == NULL) {
    log_error("[%s] Failed to allocate memory for packet.", f_path);
    return ANU_OOM;
  }

  return ANU_OK;
}

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

DEFINE_FREE(vreader_close, anu_vreader, vreader_close(&_T))

static ALWAYS_INLINE _nonnull_all_ AVStream *vreader_video_stream (
    anu_vreader *vreader) {
  return vreader->fmt_ctx->streams[vreader->video_stream_idx];
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
static size_t vreader_get_duration (anu_vreader *vreader) {

  AVStream *vid_stream = vreader_video_stream(vreader);

  /* duration in stream-base */
  int64_t duration_in_sb = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;
  log_trace("Time base for stream: `%d/%d`", stream_timebase.num,
            stream_timebase.den);

  if (duration_in_sb == AV_NOPTS_VALUE) {
    duration_in_sb =
        (vreader->fmt_ctx->duration) > 0 ? vreader->fmt_ctx->duration : 0;
    log_debug(
        "[%s] Video stream omitting duration, using container values as "
        "fallback (%.2fs)",
        vreader->fmt_ctx->url,
        anu_time_microseconds_to_seconds((size_t) duration_in_sb));
    return (size_t) duration_in_sb;
  }

  return duration_in_sb > 0 ? pts_to_useconds(duration_in_sb, stream_timebase)
                            : 0;
}

/**
 * @brief Seek to timestamp.
 *
 * Seeks to nearest preceding keyframe from target timestamp.
 *
 * @param vreader VideoReader instance.
 * @param target_ts Target time stamp (in streams own time base).
 * @return 0 on success, anything else on failure.
 *
 * @note When `av_seek_frame` fails, this function returns its error code.
 */
static inline int vreader_seek_pts (anu_vreader *vreader, int64_t target_pts) {

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking. */
  int seek_ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx,
                               target_pts, AVSEEK_FLAG_BACKWARD);

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
 * Seeks to target_pts and then decodes forward til target is reached.
 * @param vreader Video reader.
 * @param target_pts Target pts to reach.
 * @param min_pts pts of decoded frame must not be lower than this value.
 */
static int vreader_seek_and_read_to_target (anu_vreader *vreader,
                                            int64_t target_pts,
                                            int64_t min_pts) {

  int ret = vreader_seek_pts(vreader, target_pts);
  if (ret != 0) {
    return ret;
  }

  for (;;) {
    ret = video_reader_get_frame(vreader);
    if (ret != ANU_OK) {
      return ret; /* EOF or decoding error */
    }

    int64_t current_pts = vreader->frame->pts;
    if (current_pts == AV_NOPTS_VALUE) {
      current_pts = vreader->frame->best_effort_timestamp;
    }

    /* Check if we reach desired target pts OR
       we reach a frame higher than minimum pts */
    if ((current_pts >= target_pts) && (current_pts > min_pts)) {
      return ANU_OK;
    }

    av_frame_unref(vreader->frame);
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
static inline _pure_ _nonnull_ (1) bool row_has_video(const uint8_t *const row,
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
static inline bool anu_detect_black_borders (AVFrame *frame,
                                             const int threshold,
                                             cropping *crop_out) {

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
 * @param matrix [in] The matrix of values to hash, as a 1D array.
 * @param hash_algo TODO The type of hashing algorithm to use. Currently does not do anything.
 * @return Unsigned 64 bit int (hash output).
 */
static ALWAYS_INLINE uint64_t
hash_decoded_frame (uint8_t matrix[static ANU_PHASH_TOTAL_PIXELS],
                    anu_hash_type hash_algo) {

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
 * @param frame [in] Frame being scaled by the software scaler.
 * @param context [in] Software scaler instance.
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
  int dummy_src;
  int dummy_dst;
  int dummy_bright;
  int dummy_cont;
  int dummy_sat;

  /* Get default values */
  if (sws_getColorspaceDetails(context, (&inv_table), &dummy_src, (&table),
                               &dummy_dst, &dummy_bright, &dummy_cont,
                               &dummy_sat) < 0) {
    log_error("Failed to get colorspace details.");
    return -1;
  }

  /* Apply explicit ranges.
   * 1 << 16 is the fixed-point representation for "1.0" (default
   * contrast/saturation) */
  if (sws_setColorspaceDetails(context, inv_table, src_range, table, dst_range,
                               0, 1 << 16, 1 << 16) < 0) {
    log_error("Failed to set colourspace.");
    return -1;
  }
  return 0;
}

static int scale_frame (anu_vreader *vr,
                        uint8_t matrix[static ANU_PHASH_TOTAL_PIXELS],
                        int matrix_size,
                        bool crop_black) {

  AVFrame *src = vr->frame;
  char *fname = vr->fmt_ctx->url;

  cropping crop = {.x = 0, .y = 0, .w = src->width, .h = src->height};

  if (crop_black) {
    /* 24 is a safe threshold for limited-range YUV "black" */
    bool workable_frame = anu_detect_black_borders(src, 24, &crop);
    /* If returning false, then we have a fully black frame */
    if (!workable_frame) {
      log_warn("%s: Frame is completely black.", fname);
      return ANU_FRAME_BLACK;
    }

    /* Quantise the cropping to EVEN numbers only */
    crop.x &= ~1;
    crop.y &= ~1;
    crop.w &= ~1;
    crop.h &= ~1;
  }

  enum AVPixelFormat src_format = src->format;
  int src_range = (src->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

  /* HACK: Map deprecated "J" formats to standard formats and force full range pixel format.
   * This is required otherwise ffmpeg will give us the warning:
   * `deprecated pixel format used, make sure you did set range correctly`
   */
  switch (src->format) {
    case AV_PIX_FMT_YUVJ420P:
      src_format = AV_PIX_FMT_YUV420P;
      src_range = AVCOL_RANGE_JPEG;
      break;
    case AV_PIX_FMT_YUVJ422P:
      src_format = AV_PIX_FMT_YUV422P;
      src_range = AVCOL_RANGE_JPEG;
      break;
    case AV_PIX_FMT_YUVJ444P:
      src_format = AV_PIX_FMT_YUV444P;
      src_range = AVCOL_RANGE_JPEG;
      break;
    case AV_PIX_FMT_YUVJ440P:
      src_format = AV_PIX_FMT_YUV440P;
      src_range = AVCOL_RANGE_JPEG;
      break;
    default:
      break;
  }

  /* Previous sws context used */
  struct SwsContext *prev_ctx = vr->sws_ctx;

  /* Initialize the Scaler, converting pixel fmt from `src_format` to AV_PIX_FMT_GRAY8 (grayscale) */
  vr->sws_ctx = sws_getCachedContext(vr->sws_ctx, crop.w, crop.h, src_format,
                                     matrix_size, matrix_size, AV_PIX_FMT_GRAY8,
                                     SWS_FAST_BILINEAR, NULL, NULL, NULL);

  if (!vr->sws_ctx) {
    log_error("%s: Failed to create scaling context.", fname);
    return ANU_LIBAV_FAIL;
  }

  /* Normalise colourspaces IF sws_ctx is not the same as the previous context */
  if (prev_ctx != vr->sws_ctx) {
    if (normalise_sws_colourspace(vr->sws_ctx, src_range)) {
      log_error("%s: Colourspace normalisation failed.", fname);
      return ANU_LIBAV_FAIL;
    }
  }

  const AVPixFmtDescriptor *pixelfmt_desc = av_pix_fmt_desc_get(src_format);

  if (!pixelfmt_desc) {
    log_error("%s: No pixel format description found.", fname);
    return ANU_LIBAV_FAIL;
  }

  /* Fetch horizontal and vertical shift from pixel format */
  int h_shift = pixelfmt_desc->log2_chroma_w;
  int v_shift = pixelfmt_desc->log2_chroma_h;

  const uint8_t *src_slices[4] = {0};
  int src_linesizes[4] = {0};

  /* Advance pointers for all available planes based on cropping */
  for (int i = 0; (i < 4 && src->data[i]); i++) {
    /* Chroma planes (usually 1 and 2) need to be shifted depending on subsampling */
    int x_shift = (i == 1 || i == 2) ? h_shift : 0;
    int y_shift = (i == 1 || i == 2) ? v_shift : 0;

    /* Find the bytes per pixel step for this specific plane */
    int bytes_per_pixel = pixelfmt_desc->comp[0].step;
    src_slices[i] = src->data[i] +
                    ((ptrdiff_t) (crop.y >> y_shift) * src->linesize[i]) +
                    ((ptrdiff_t) (crop.x >> x_shift) * bytes_per_pixel);

    src_linesizes[i] = src->linesize[i];
  }

  /* Setup destination pointers to write DIRECTLY into flat matrix */
  uint8_t *dst_slices[4] = {matrix, NULL, NULL, NULL};
  int dst_linesizes[4] = {matrix_size, 0, 0, 0};

  int scaling_ret = sws_scale(vr->sws_ctx, src_slices, src_linesizes, 0, crop.h,
                              dst_slices, dst_linesizes);

  if (scaling_ret <= 0) {
    log_error("%s: Scaling FAILED: `%s`", fname, av_err2str(scaling_ret));
    return ANU_LIBAV_FAIL;
  }

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
static int video_reader_get_frame (anu_vreader *vreader) {
  int ret = 0;

  AVCodecContext *codec_ctx = vreader->codec_ctx;
  while (1) {
    /* Try to receive a frame first */
    ret = avcodec_receive_frame(codec_ctx, vreader->frame);

    /* Successfully got a frame */
    if (ret == 0) {
      return ANU_OK;
    }

    if (ret == AVERROR(EOF)) {
      return ret;
    }

    if (ret != AVERROR(EAGAIN)) {
      /* If it's not EAGAIN and not EOF, it's a real error */
      log_error("%s Error receiving frame: %s", vreader->fmt_ctx->url,
                av_err2str(ret));
      return ret;
    }
    AVPacket *packet = vreader->packet;
    /* Read a frame */
    ret = av_read_frame(vreader->fmt_ctx, packet);

    if (ret == AVERROR(EOF) || ret == AVERROR(EINVAL)) {
      /* EOF reached on the container
         We need to send a NULL packet to the decoder
         to "flush" out any delayed or cached frames. */
      avcodec_send_packet(codec_ctx, NULL);
      /* Loop back to receive the flushed frames */
      continue;
    }
    /* Ignore non-video streams */
    if (packet->stream_index != vreader->video_stream_idx) {
      av_packet_unref(packet);
      continue;
    }
    ret = avcodec_send_packet(codec_ctx, packet);
    av_packet_unref(packet);

    if (ret < 0) {
      log_error("%s Decoding error: %s", vreader->fmt_ctx->url,
                av_err2str(ret));
      return ret;
    }
  }
}

typedef struct filter_ctx {
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
  int init;
} filter_ctx;

static int init_rotation_filter_graph (filter_ctx *fctx,
                                       AVFrame *frame,
                                       AVRational time_base,
                                       int rotation_normalised) {
  char args[512];
  int ret = 0;

  enum FILTER_FOR_ANGLE { _90_DEGREES = 0, _180_DEGREES = 1, _270_DEGREES = 2 };

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

  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           frame->width, frame->height, frame->format, time_base.num,
           time_base.den, frame->sample_aspect_ratio.num,
           frame->sample_aspect_ratio.den);

  ret = avfilter_graph_create_filter(&fctx->buffersrc_ctx, buffersrc, "in",
                                     args, NULL, fctx->filter_graph);
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

  ret = avfilter_graph_create_filter(&fctx->buffersink_ctx, buffersink, "out",
                                     NULL, NULL, fctx->filter_graph);
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

  ret = avfilter_graph_parse_ptr(fctx->filter_graph, filter_desc, &inputs,
                                 &outputs, NULL);
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

enum ANU_STATUS anu_video_hash (anu_file *file,
                                anu_config *config,
                                uint64_t *hashes_out,
                                uint64_t *frame_timestamps_out) {

  assert(config->segments > 0);
  assert(file);
  assert(hashes_out);

  anu_vreader vreader __free(vreader_close) = {0};

  /* Setup video reader */
  int code = 0;
  code = vreader_init(file->path, &vreader);
  if (code != ANU_OK) {
    return code;
  }

  file->duration_us = vreader_get_duration(&vreader);

  /* Return early if duration is 0 */
  if (file->duration_us == 0) {
    return ANU_VIDEO_LEN_SHORT;
  }
  char *fname = anu_file_get_filename(file);

  if (file->duration_us < config->segments) {
    log_warn("[%s] Video too short for the requested number of segments.",
             fname);
    return ANU_VIDEO_LEN_SHORT;
  };

  /* As long as this is true we won't break anything when we cast for libav */
  assert(file->duration_us < INT64_MAX);

  /* Check if file duration is longer than the skip threshold */
  if (file->duration_us <=
      (anu_time_seconds_to_microseconds((double) config->skip_duration))) {
    log_debug(
        "[%s] Skipping - Duration (%.2f seconds) less than threshold (%zu "
        "seconds)",
        fname, anu_time_microseconds_to_seconds(file->duration_us),
        config->skip_duration);

    return ANU_VIDEO_LEN_SHORT;
  }

  const size_t frame_step_us = file->duration_us / config->segments;
  /* Counter for # of frames successfully decoded */
  int frames_decoded = 0;

  /* Target timestamp in microseconds */
  int64_t seek_target_us = 0;
  const size_t seek_target_us_jump = (frame_step_us / 2);

  uint8_t matrix[ANU_PHASH_TOTAL_PIXELS] = {0};
  /* Video stream */
  AVStream *vid_stream_ptr = vreader_video_stream(&vreader);

  const AVRational stream_timebase = vid_stream_ptr->time_base;
  /* Previously decoded frames PTS */
  int64_t last_pts = -1;

  /* Filter context in case we need to run any filters on frames */
  filter_ctx fctx = {0};
  /* Filtered frame */
  AVFrame *filtered_frame = NULL;

  /* Check for whether stream should be rotated (this is a metadata check) */
  int rotation = get_video_stream_rotation(&vreader);
  int rotation_normalised = normalise_angle_360(rotation);
  if (rotation_normalised) {
    log_info("[%s]: Detected rotation: %d degrees (%d degrees normalised)\n",
             vreader.fmt_ctx->url, rotation, rotation_normalised);
    filtered_frame = av_frame_alloc();
  }

  for (size_t i = 0; i < config->segments; i++) {

    /* Target to seek to in microseconds */
    seek_target_us = (int64_t) ((i * frame_step_us) + seek_target_us_jump);

    /* Target timestamp in streams time base (tick) */
    int64_t seek_target_sb =
        av_rescale_q(seek_target_us, AV_TIME_BASE_Q, stream_timebase);

    int errcode = 0;

    log_trace("[%s] Segment [%zu/%zu] -> Attempting seek to PTS '%ld' (%.1f s)",
              fname, i + 1, config->segments, seek_target_sb,
              anu_time_microseconds_to_seconds((size_t) seek_target_us));

    /* Seek to timestamp */
    errcode =
        vreader_seek_and_read_to_target(&vreader, seek_target_sb, last_pts);
    if (errcode != ANU_OK) {
      log_error("[%s] Could not seek to segment `%zu` (PTS `%ld`): %s", fname,
                i, seek_target_sb, av_err2str(errcode));
      goto failure;
    }

    int64_t frame_pts_sb = (vreader.frame->pts != AV_NOPTS_VALUE)
                               ? vreader.frame->pts
                               : vreader.frame->best_effort_timestamp;
    size_t frame_pts_us =
        pts_to_useconds(frame_pts_sb, vid_stream_ptr->time_base);
    double frame_pts_s = anu_time_microseconds_to_seconds(frame_pts_us);

    /* After seeking to the necessary timestamp, we want to retrieve the frame */
    errcode = video_reader_get_frame(&vreader);
    if (errcode != ANU_OK) {
      log_error("[%s] Could not decode frame for pts target `%ld`: `%s`", fname,
                seek_target_sb, av_err2str(errcode));
      goto failure;
    }

    log_info(
        "[%s] Segment [%zu/%zu] -> Decoded Frame (PTS='%ld' us='%ld' "
        "s='%.1f') ",
        fname, (i + 1), config->segments, frame_pts_sb, frame_pts_us,
        frame_pts_s);

    /* Keep track of frame PTS so we can seek to a higher one next iteration */
    last_pts = frame_pts_sb;

    /* If there is a rotation required, then do it now: */
    if (rotation_normalised) {

      /* If filter context not initialised, lets initialise it now */
      if (!fctx.init) {
        int ret = init_rotation_filter_graph(
            &fctx, vreader.frame, stream_timebase, rotation_normalised);
        if (ret < 0) {
          log_error("[%s] Failed to init filter graph: %s", fname,
                    av_err2str(ret));
          goto failure;
        }
        fctx.init = 1;
      }

      /* Add frame to filter */
      errcode = av_buffersrc_add_frame_flags(fctx.buffersrc_ctx, vreader.frame,
                                             AV_BUFFERSRC_FLAG_KEEP_REF);
      if (errcode < 0) {
        goto failure;
      }

      /* Retrieve filtered frame */
      errcode = av_buffersink_get_frame(fctx.buffersink_ctx, filtered_frame);
      if (errcode < 0) {
        goto failure;
      }

      /* Swap original frame out with the new filtered one. */
      av_frame_unref(vreader.frame);
      av_frame_move_ref(vreader.frame, filtered_frame);
    }

    /* Scale down frame to 32x32 and check for black bars */
    errcode = scale_frame(&vreader, matrix, ANU_PHASH_INPUT_SIZE,
                          ANU_HAS_ANY_FLAG(config->detect_flags, DETECT_BARS));
    if (errcode != ANU_OK) {
      log_error("[%s] Failed to scale frame: `%s`", fname,
                (errcode == ANU_FRAME_BLACK) ? "Frame was found to be too dark."
                                             : av_err2str(errcode));

      goto failure;
    }

    /*
     * If everything was SUCCESSFUL
     */
    hashes_out[i] = hash_decoded_frame(matrix, config->hash_algorithm);
    frame_timestamps_out[i] = (u64) frame_pts_sb;
    log_info("[%s] Frame '%ld' => %lX", fname, frame_pts_sb, hashes_out[i]);
    ++frames_decoded;

    /* NOTE: Continue before we fall into the failure label */
    continue;

    /*
     * Jump here when an error occurs and we want to skip over this particular hash.
     * We assign 0's in order to indicate an error has occured and that a hash was not produced.
     */
  failure:
    {
      hashes_out[i] = 0;
      frame_timestamps_out[i] = 0;
      log_warn("%s (%zu/%zu) could not be hashed.", fname, (i + 1),
               config->segments);
    }
  }

  if (filtered_frame) {
    av_frame_free(&filtered_frame);
  }
  if (fctx.init) {
    avfilter_graph_free(&fctx.filter_graph);
  }

  log_trace("[%s] DONE. Processed %d frames.", fname, frames_decoded);
  return ANU_OK;
}
