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
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SAVE_HASH_PGM 0
#define MATRIX_BUF_SIZE 32
#define MAX_SEGMENTS 20

void debug_print_matrix(double* matrix, int rows, int cols);

/* Helper to visualise matrix */
void debug_print_matrix (double* matrix, int rows, int cols) {
  printf("--- %dx%d Visual Dump ---\n", cols, rows);
  for (int y = 0; y < rows; y += 2) {  // Skip every other row to fit screen
    for (int x = 0; x < cols; x++) {
      double val = matrix[(y * cols) + x];
      /* Simple ASCII mapping */
      char c = ' ';
      if (val > 200) {
        c = '#';
      } else if (150 < val) {
        c = '+';
      } else if (100 < val) {
        c = ':';
      } else if (50 < val) {
        c = '.';
      }
      printf("%c", c);
    }
    printf("\n");
  }
  printf("-------------------------\n");
}

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

/**
 * @brief Initialise a grayscale frame of specified width and height.
 **/
int init_gray_frame (AVFrame* output, int width, int height) {
  output->height = height;
  output->width = width;
  output->format = AV_PIX_FMT_GRAY8;

  if (av_frame_get_buffer(output, 0) != 0) {
    av_frame_free(&output);
    fprintf(stderr, "Could not initialise grayscale frame buffer.\n");
    return 1;
  }

  return 0;
}

uint64_t average_hash (AVFrame* src_frame) {
  uint64_t hash = 0;
  int width = 8;
  int height = 8;

  /* Create small frame to hold shrunken src frame */
  AVFrame* smallframe;
  smallframe = av_frame_alloc();
  if (smallframe == NULL) {
    fprintf(stderr, "Could not allocate memory.\n");
    return 1;
  }

  if (init_gray_frame(smallframe, width, height)) {
    abort();
  }

  if (scale_frame(src_frame, smallframe, width, height)) {
    fprintf(stderr, "Failed to scale frame!");
    av_frame_free(&smallframe);
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

/* Helper for the coefficient scaling factor in DCT-II
 *   C(u) = 1/sqrt(2) if u=0, else 1 */
static double dct_c (int u) {
  if (u == 0) {
    return 1.0 / sqrt(2.0);
  }
  return 1.0;
}

uint64_t calculate_dct_phash (double* input_matrix_flat, int rows, int cols) {
  /* Intermediate storage */
  double row_result[MATRIX_BUF_SIZE][MATRIX_BUF_SIZE];
  /* Final result */
  double dct_result[MATRIX_BUF_SIZE][MATRIX_BUF_SIZE];

  double N = rows;
  double scale_factor = sqrt(2.0 / N);

  /* Pass 1: 1D DCT on Rows */
  for (int y = 0; y < N; y++) {
    for (int u = 0; u < N; u++) {
      double sum = 0.0;
      for (int x = 0; x < N; x++) {
        /* Formula: sum += pixel[x] * cos(...) */
        sum += input_matrix_flat[(y * cols) + x] *
               cos(((2 * x + 1) * u * M_PI) / (2 * N));
      }
      row_result[y][u] = scale_factor * dct_c(u) * sum;
    }
  }

  /* Pass 2: 1D DCT on Columns (applied to row_result) */
  for (int x = 0; x < N; x++) {
    for (int v = 0; v < N; v++) {
      double sum = 0.0;
      for (int y = 0; y < N; y++) {
        sum += row_result[y][x] * cos(((2 * y + 1) * v * M_PI) / (2 * N));
      }
      dct_result[v][x] = scale_factor * dct_c(v) * sum;
    }
  }

  double sum_pixels = 0.0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (x == 0 && y == 0) {
        continue;  // Skip DC
      }
      sum_pixels += dct_result[y][x];
    }
  }

  /* Average of 63 coefficients (8*8 - 1) */
  double average = sum_pixels / 63.0;

  /* Build the 64-bit hash */
  uint64_t final_hash = 0;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {

      final_hash <<= 1;
      /* Shift hash to make room for new bit
       * (Order of iteration determines bit position, row-major is standard)
       * Note: Standard pHash usually sets bit based on >= average */
      if (y == 0 && x == 0) {
        continue;
      }

      if (dct_result[y][x] >= average) {
        final_hash |= 1;
      }
    }
  }

  debug_print_matrix(&dct_result[0][0], 32, 32);
  return final_hash;
}

uint64_t dct_hash (AVFrame* src_frame) {
  uint64_t final_hash = 0;
  int width = MATRIX_BUF_SIZE;
  int height = MATRIX_BUF_SIZE;

  AVFrame* gray = av_frame_alloc();
  if (gray == NULL) {
    fprintf(stderr, "Failed to initialise gray frame.\n");
    abort();
  }
  if (init_gray_frame(gray, width, height)) {
    abort();
  }

  if (scale_frame(src_frame, gray, width, height)) {
    fprintf(stderr, "Failed to scale frame!");
    /* Clean up before aborting */
    av_frame_free(&gray);
    abort();
  }

  double matrix[MATRIX_BUF_SIZE][MATRIX_BUF_SIZE] = {0};

  for (int y = 0; y < height; y++) {
    uint8_t* row_ptr = gray->data[0] + ((ptrdiff_t)y * gray->linesize[0]);
    for (int x = 0; x < width; x++) {
      matrix[y][x] = (double)row_ptr[x];
    }
  }

  final_hash = calculate_dct_phash(&matrix[0][0], 32, 32);
  av_frame_free(&gray);

  return final_hash;
}

/* This is the function called ONLY when a valid frame is fully decoded */
static void hash_decoded_frame (VideoReader* vreader, anuHashType hash_algo,
                                uint64_t* hash_out) {

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
  *hash_out = hash;
  printf("Hash: %016" PRIx64 "\n", *hash_out);
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
      return 0;
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
  return 0;
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
    errcode = 0;
  }

  /* Will read bytes from file/decode a few frames to fill out context that the
     method above missed (`avformat_open_input` will only read header of file)
   */
  errcode = avformat_find_stream_info(vreader->fmt_ctx, NULL);
  if (errcode < 0) {
    fprintf(stderr, "Could not find stream info: `%s`\n", av_err2str(errcode));
    got_info = false;
    errcode = 0;
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

int hash_video (char* filename, uint64_t* hashes_out, int segments,
                anuHashType hash_algo) {

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
  for (int i = 0; i < total_video_segments; i++) {
    decoding_success = 0;
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
      continue;  // Try next segment
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

        long current_pts = vreader.frame->best_effort_timestamp;
        if (current_pts < seek_target_sb) {
          /* printf("Skipping frame at PTS %ld (Target: %ld)\n", current_pts,
           * target_timestamp); */
          av_packet_unref(vreader.packet);
          continue; /* Loop again to get next frame */
        }

        hash_decoded_frame(&vreader, hash_algo, &hashes_out[frames_decoded]);
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
int main (int argc, char* argv[]) {  // NOLINT (unused-*)
  char* filename = (argc > 1) ? argv[1] : "./tulsi.mov";
  char* filename2 = "tulsi2.mov";

  const int SEGMENTS = 10;

  uint64_t hashes_vidA[MAX_SEGMENTS];
  uint64_t hashes_vidAvg[MAX_SEGMENTS];
  uint64_t hashes_vidB[MAX_SEGMENTS];

  hash_video(filename, &hashes_vidA[0], SEGMENTS, ANUHASH_AVG);
  hash_video(filename2, &hashes_vidB[0], SEGMENTS, ANUHASH_AVG);

  are_videos_duplicate(hashes_vidA, hashes_vidB, SEGMENTS);

  return 0;
}
