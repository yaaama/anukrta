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
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "explore.h"
#include "hash.h"
#include "log.h"
#include "util.h"

/* Hardcode this so I don't have to include another header */
#define ANU_EAGAIN 11

int video_reader_grab_frame_at_pts(anu_vreader *vreader, long target_pts);
int scale_frame(anu_vreader *vr, size_t width, size_t height,
                AVFrame *out_frame);
uint64_t hash_decoded_frame(uint8_t *matrix, anu_hash_type hash_algo);
int decode_avpacket(anu_vreader *vreader);
size_t pts_to_useconds(long pts, AVRational timebase);
double frame_pts_to_seconds(long pts, AVRational timebase);
void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *prefix, long frame_num);
void copy_frame_to_buffer(AVFrame *frame, uint8_t *dest, int width);
int normalise_sws_colourspace(AVFrame *frame, SwsContext *context);
int grey_frame_init(int width, int height, AVFrame *out_frame);
int vreader_init(char *f_path, anu_vreader *vreader);
void vreader_close(anu_vreader *vreader);
int vreader_seek_pts(anu_vreader *vreader, int64_t target_pts);
size_t vreader_get_duration(anu_vreader *vreader);
AVStream *vreader_video_stream(anu_vreader *vreader);

int anu_video_hash (anu_file *file, anukrta_config *config,
                    uint64_t *hashes_out) {

  assert(config->segments > 0 && file && hashes_out);

  anu_vreader vreader = {0};

  /* Setup video reader */
  if (vreader_init(file->path, &vreader) < 0) {
    vreader_close(&vreader);
    return -1;
  }

  file->duration_us = vreader_get_duration(&vreader);
  if (file->duration_us == 0) {
    vreader_close(&vreader);
    return -1;
  }
  char *fname = anu_file_get_filename(file);

  /* We want to split the video into this many segments */
  size_t total_video_segments = config->segments;

  size_t video_duration_us = file->duration_us;
  assert(video_duration_us != 0);
  assert(video_duration_us > total_video_segments);
  /* As long as this is true we won't break anything when we cast for libav */
  assert(video_duration_us < INT64_MAX);

  if (!file->duration_us) {
    file->duration_us = video_duration_us;
  }

  /* Check if file duration is longer than the skip threshold */
  if (file->duration_us <= config->skip_duration) {
    log_debug("[%s] Skipping - Duration less than threshold (%.1f < %.1f) ",
              fname, anu_time_microseconds_to_seconds(file->duration_us),
              anu_time_microseconds_to_seconds(config->skip_duration));

    vreader_close(&vreader);
    return -2;
  }

  AVFrame *gray_frame = av_frame_alloc();
  if (!gray_frame || grey_frame_init(ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                                     gray_frame) != 0) {
    log_fatal("[%s] Failed to allocate grey frame.", fname);
    av_frame_free(&gray_frame);
    vreader_close(&vreader);
    return -1;
  }

  size_t frame_step_us = video_duration_us / total_video_segments;
  /* Counter for # of frames successfully decoded */
  int frames_decoded = 0;
  /* Target timestamp in microseconds */
  int64_t seek_target_us = 0;
  /* Target timestamp in streams time base (tick) */
  int64_t seek_target_sb = 0;

  /* Return value of `decode_packet` */
  int decoding_success = 0;
  /* Loop will turn this true when we have decoded a frame for the segment */
  bool frame_found_for_segment = false;
  long current_pts = 0;
  uint8_t matrix[ANU_PHASH_INPUT_SIZE * ANU_PHASH_INPUT_SIZE] = {0};
  /* Video stream */
  AVStream *vid_stream_ptr = vreader_video_stream(&vreader);

  for (size_t i = 0; i < total_video_segments; i++) {

    seek_target_us = (int64_t) (i * frame_step_us);
    /* NOTE: * As long as our duration values are positive, all of this casting is fine */
    seek_target_sb =
        av_rescale_q(seek_target_us, AV_TIME_BASE_Q, vid_stream_ptr->time_base);

    log_debug("[%s] --- Segment [%zu/%zu] ---", fname, i + 1,
              total_video_segments);
    log_debug("[%s] Seeking to PTS %ld (%.1f seconds)", fname, seek_target_sb,
              anu_time_microseconds_to_seconds((size_t) seek_target_us));

    /* Seek to timestamp */
    if (vreader_seek_pts(&vreader, seek_target_sb) < 0) {
      log_warn("[%s] Could not seek to segment `%zu`", fname, i);
      continue; /* Try next segment */
    }

    if (video_reader_grab_frame_at_pts(&vreader, seek_target_sb) != 1) {
      log_debug("[%s] Could not get frame at PTS %ld, segment [%zu]", fname,
                seek_target_sb, i);
      continue;
    }

    if (scale_frame(&vreader, ANU_PHASH_INPUT_SIZE, ANU_PHASH_INPUT_SIZE,
                    gray_frame) != 0) {
      log_error("[%s] Failed to scale frame for segment `%zu`", fname, i);
      continue;
    }

    copy_frame_to_buffer(gray_frame, matrix, ANU_PHASH_INPUT_SIZE);

    hashes_out[frames_decoded] =
        hash_decoded_frame(&matrix[0], config->hash_algorithm);

    log_debug("[%s] Frame '%ld' => %lX", fname, vreader.codec_ctx->frame_num,
              hashes_out[frames_decoded]);
    frames_decoded++;
  }

  /* Cleanup */
  av_frame_free(&gray_frame);
  vreader_close(&vreader);

#ifdef ANU_DEBUG

  char hashes[1024];
  int total_len = 0;
  for (int i = 0; i < frames_decoded; i++) {
    int end = sprintf(&hashes[total_len], " %lX,", hashes_out[i]);
    total_len += end;
    hashes[total_len] = ' ';
  }
  hashes[total_len] = '\0';
  log_debug("[%s] DONE => {%s}\n", fname, hashes);
#endif
  log_trace("[%s] DONE. Processed %d frames.", fname, frames_decoded);
  return 0;
}

/* This is the function called ONLY when a valid frame is fully decoded */
uint64_t hash_decoded_frame (uint8_t *matrix, anu_hash_type hash_algo) {
  uint64_t hash = 0;
  switch (hash_algo) {
    case ANU_HASH_ALGO_DCT:
      {
        hash = dct_hash(matrix);
        break;
      }
    default:
      {
        log_error("Hashing algorithm not specified.");
      }
  }

  if (hash == 0) {
    log_warn("Received a 0 value for hash.");
  }

  return hash;
}

size_t pts_to_useconds (int64_t pts, AVRational timebase) {
  assert(pts >= 0);
  return (size_t) av_rescale_q(pts, timebase, AV_TIME_BASE_Q);
}

double frame_pts_to_seconds (int64_t pts, AVRational timebase) {
  assert(pts >= 0);
  return ((double) av_rescale_q(pts, timebase, AV_TIME_BASE_Q) / 1000000);
}

void save_gray_frame (unsigned char *buf, int wrap, int xsize, int ysize,
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
    fwrite(buf + ((ptrdiff_t) index * wrap), 1, (unsigned long) xsize, fptr);
  }
  fclose(fptr);
}

void copy_frame_to_buffer (AVFrame *frame, uint8_t *dest, int width) {
  assert(width > 0);
  /* Access the raw data pointer for the first plane (Y / Grayscale) */
  uint8_t *src_data = frame->data[0];
  int src_linesize = frame->linesize[0];

  for (long y = 0; y < width; y++) {
    /* Calculate the start of the row in the source frame */
    /* uint8_t *src_row = src_data + (y * src_linesize); */
    /* Calculate the start of the row in the destination buffer */
    /* uint8_t *dest_row = dest + (y * width); */
    memcpy((dest + (y * width)), (src_data + (y * src_linesize)),
           (unsigned long) width);
  }
}

int normalise_sws_colourspace (AVFrame *frame, SwsContext *context) {

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

int scale_frame (anu_vreader *vr, size_t width, size_t height,
                 AVFrame *out_frame) {

  enum AVPixelFormat input_fmt = vr->frame->format;

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
  AVFrame *src = vr->frame;

  vr->sws_ctx = sws_getCachedContext(
      vr->sws_ctx, src->width, src->height, input_fmt, out_frame->width,
      out_frame->height, out_frame->format, SWS_AREA, NULL, NULL, NULL);

  if (!vr->sws_ctx) {
    log_error("Failed to create scaling context.");
    return 1;
  }

  /* Normalise colourspaces */
  if (normalise_sws_colourspace(vr->frame, vr->sws_ctx)) {
    log_error("Colourspace normalisation failed.");
    return 1;
  }

  int scaling_ret = sws_scale_frame(vr->sws_ctx, out_frame, vr->frame);
  if (scaling_ret <= 0) {
    log_error("Scaling FAILED: `%s`", av_err2str(scaling_ret));
    return 1;
  }

  return 0;
}

/**
 * @brief Initialise a grayscale frame of specified width and height.
 **/
int grey_frame_init (int width, int height, AVFrame *out_frame) {
  out_frame->height = height;
  out_frame->width = width;
  out_frame->format = AV_PIX_FMT_GRAY8;

  if (av_frame_get_buffer(out_frame, 0) != 0) {
    log_error("Could not initialise grayscale frame buffer.");
    return 1;
  }

  return 0;
}

int video_reader_grab_frame_at_pts (anu_vreader *vreader, long target_pts) {
  int decoding_status = 0;

  while (av_read_frame(vreader->fmt_ctx, vreader->packet) >= 0) {
    if (vreader->packet->stream_index != vreader->video_stream_idx) {
      av_packet_unref(vreader->packet);
      continue;
    }

    decoding_status = decode_avpacket(vreader);

    if (decoding_status == 1) {
      long current_pts = vreader->frame->best_effort_timestamp;
      if (current_pts >= target_pts) {
        av_packet_unref(vreader->packet);
        return 1; /* Success, frame found */
      }
    } else if (decoding_status < 0) {
      av_packet_unref(vreader->packet);
      return -1; /* Decoding Error */
    }

    av_packet_unref(vreader->packet);
  }
  return 0; /* EOF */
}

int decode_avpacket (anu_vreader *vreader) {
  /* Send packet to decoder */
  int ret = avcodec_send_packet(vreader->codec_ctx, vreader->packet);

  /* Check if it was successful */
  if (ret != 0) {
    log_error("Failed sending packet: %s", av_err2str(ret));
    return ret;
  }

  /* NOTE: A single may contain 0 frames, or MANY frames. */
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
 * @param f_path[in] File path.
 * @param vreader[out] Structure to initialise.
 * @return 0 if success, non-zero for anything else.
 *
 */
int vreader_init (char *f_path, anu_vreader *vreader) {

  /* Initialise VideoReader */
  memset(vreader, 0, sizeof(anu_vreader));
  vreader->video_stream_idx = -1;

  log_debug("Opening `%s`", f_path);

  bool got_info = true;

  /* Opens input file and guesses format of file */
  int errcode = 0;
  errcode = avformat_open_input(&vreader->fmt_ctx, f_path, NULL, NULL);
  if (errcode < 0) {
    log_debug("Could not open file (`%s`): `%s`", f_path, av_err2str(errcode));
    return -1;
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
    log_error("Failed to read header/stream for file %s", f_path);
    return -1;
  }

  /* Find Video Stream & Codec */
  bool decoder_found = true;
  bool stream_found = true;
  const AVCodec *codec = NULL;

  /* Finds best stream that matches our specifications */
  vreader->video_stream_idx = av_find_best_stream(
      vreader->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, -1);

  if (vreader->video_stream_idx == AVERROR_DECODER_NOT_FOUND) {
    log_error("No decoder found for stream.");
    decoder_found = false;
  } else if (vreader->video_stream_idx == AVERROR_STREAM_NOT_FOUND) {
    log_error("No video stream found.");
    stream_found = false;
  }

  if (!stream_found || !decoder_found) {
    return -1;
  }

  log_trace("Found video stream at index `%d`", vreader->video_stream_idx);

  AVCodecParameters *codec_params = NULL;
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
    return -1;
  }

  return 0;
}

void vreader_close (anu_vreader *vreader) {
  if (vreader->packet) {
    av_packet_free(&vreader->packet);
  }
  if (vreader->sws_ctx) {
    sws_freeContext(vreader->sws_ctx);
  }

  if (vreader->frame) {
    av_frame_free(&vreader->frame);
  }
  if (vreader->codec_ctx) {
    /* Drain decoder */
    avcodec_send_packet(vreader->codec_ctx, NULL);
    /* Free context */
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
size_t vreader_get_duration (anu_vreader *vreader) {
  AVStream *vid_stream = vreader_video_stream(vreader);

  /* duration in stream-base */
  int64_t duration_in_sb = vid_stream->duration;
  AVRational stream_timebase = vid_stream->time_base;
  log_trace("Time base for stream: `%d/%d`", stream_timebase.num,
            stream_timebase.den);

  if (duration_in_sb > 0) {
    return pts_to_useconds(duration_in_sb, stream_timebase);
  }

  if (duration_in_sb == AV_NOPTS_VALUE) {
    duration_in_sb =
        (vreader->fmt_ctx->duration) > 0 ? vreader->fmt_ctx->duration : 0;
    log_warn(
        "[%s] Video stream omitting duration, using container values as "
        "fallback (%.2fs)",
        vreader->fmt_ctx->url,
        anu_time_microseconds_to_seconds((size_t) duration_in_sb));
    return (size_t) duration_in_sb;
  }

  return 0;
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
int vreader_seek_pts (anu_vreader *vreader, int64_t target_pts) {

  /* Flush the decoder buffers.
   *   If we don't do this, the decoder might return cached frames from the
   *   old position before decoding frames from the new position. */
  avcodec_flush_buffers(vreader->codec_ctx);

  /* Perform seek
   *   AVSEEK_FLAG_BACKWARD: If the exact TS isn't a keyframe,
   jump to the nearest keyframe BEFORE this timestamp.
   *   AVSEEK_FLAG_FRAME: Tells ffmpeg to interpret the target as a specific
   * frame number (rarely works well), so we stick to TimeStamp seeking. */
  int ret = av_seek_frame(vreader->fmt_ctx, vreader->video_stream_idx,
                          target_pts, AVSEEK_FLAG_BACKWARD);

  if (ret < 0) {
    log_warn("Error seeking to timestamp %ld: %s", target_pts, av_err2str(ret));
    return ret;
  }

  return ret;
}

ALWAYS_INLINE AVStream *vreader_video_stream (anu_vreader *vreader) {
  assert(vreader);
  assert(vreader->video_stream_idx >= 0);
  assert(vreader->fmt_ctx);
  return vreader->fmt_ctx->streams[vreader->video_stream_idx];
}
