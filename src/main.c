/* Video similarity tool */
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
  /* File (container/AV file) context
   * AVFormatContext holds the header information stored in file (container) */
  AVFormatContext* fmt_ctx;
  /* Video encoding context.
Codec is used to decode the video stream */
  AVCodecContext* codec_ctx;

  /* Index of video stream inside container */
  int video_stream_idx;
  /* Frame */
  AVFrame* frame;
  /* Packet (compressed frame of audio/video) */
  AVPacket* packet;
} VideoReader;

static void save_gray_frame (unsigned char* buf, int wrap, int xsize,
                             int ysize,  // NOLINT(*swappable-parameters)
                             long frame_num) {
  FILE* fptr;

  char filename[1024];
  snprintf(filename, sizeof(filename), "frame-%ld.pgm", frame_num);
  fptr = fopen(filename, "w");

  if (!fptr) {
    perror("Failure saving gray scale image, could not open file.");
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

/* This is the function called ONLY when a valid frame is fully decoded */
static void process_valid_frame (VideoReader* vreader) {

  printf("Frame `%ld` decoded! Res: `%dx%d`, PTS: `%ld`\n",
         vreader->codec_ctx->frame_num, vreader->frame->width,
         vreader->frame->height, vreader->frame->pts);

  // Warning validation
  if (vreader->frame->format != AV_PIX_FMT_YUV420P &&
      vreader->frame->format != AV_PIX_FMT_GRAY8) {
    fprintf(stderr, "Warning: Format is not YUV420P or Grayscale.\n");
  }

  // Save to disk (Your business logic)
  save_gray_frame(vreader->frame->data[0], vreader->frame->linesize[0],
                  vreader->frame->width, vreader->frame->height,
                  vreader->codec_ctx->frame_num);
}

/* Returns: 0 on success (even if no frame produced yet), negative on critical */
/* error */
int decode_packet (VideoReader* vreader, int* frames_processed) {
  // 1. Send packet to decoder
  int ret = avcodec_send_packet(vreader->codec_ctx, vreader->packet);
  if (ret < 0) {
    fprintf(stderr, "Error sending packet: `%s`\n", av_err2str(ret));
    return ret;
  }

  /* 2. Loop to pull frames (A single packet might contain multiple frames, or 0) */
  while (ret >= 0) {
    ret = avcodec_receive_frame(vreader->codec_ctx, vreader->frame);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      /* Not an error. Just means we need more packets or stream is done. */
      break;
    }
    if (ret < 0) {
      fprintf(stderr, "Error receiving frame: %s\n", av_err2str(ret));
      return ret;
    }

    /* We have a frame!  */
    process_valid_frame(vreader);
    ++(*frames_processed);
  }
  return 0;
}

int open_video_reader (char* filename, VideoReader* vreader) {

  /* 0.
   * Initialise VideoReader
   */
  vreader->fmt_ctx = NULL;
  vreader->codec_ctx = NULL;
  vreader->frame = NULL;
  vreader->packet = NULL;
  vreader->video_stream_idx = -1;

  printf("Opening file `%s`", filename);

  /* 1.
   * Open input file and read header data
   * avformat_find_stream_info will populate `AVFormatContext` struct.
   */
  if (avformat_open_input(&vreader->fmt_ctx, filename, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open file %s\n", filename);
    return -1;
  }
  if (avformat_find_stream_info(vreader->fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info\n");
    return -1;
  }

  printf("\nSearching container for video stream...\n");

  /* 3.
   * Find Video Stream & Codec */
  const AVCodec* codec = NULL;
  AVCodecParameters* codec_params = NULL;

  for (unsigned int i = 0; i < vreader->fmt_ctx->nb_streams; i++) {
    AVCodecParameters* local_p = vreader->fmt_ctx->streams[i]->codecpar;
    const AVCodec* local_c = avcodec_find_decoder(local_p->codec_id);

    if (local_p->codec_type == AVMEDIA_TYPE_VIDEO && !codec) {
      codec = local_c;
      codec_params = local_p;
      vreader->video_stream_idx = i;  // NOLINT (*narrowing-conversions)
      printf("Found Video Stream #%d (Codec: %s)\n", i, codec->name);
    }
  }

  if (vreader->video_stream_idx == -1) {
    fprintf(stderr, "No video stream found.\n");
    return -1;
  }

  // 4. Init Codec Context
  vreader->codec_ctx = avcodec_alloc_context3(codec);
  if (!vreader->codec_ctx) {
    return -1;
  }

  if (avcodec_parameters_to_context(vreader->codec_ctx, codec_params) < 0) {
    return -1;
  }
  if (avcodec_open2(vreader->codec_ctx, codec, NULL) < 0) {
    return -1;
  }

  // 5. Alloc Buffers
  vreader->frame = av_frame_alloc();
  vreader->packet = av_packet_alloc();

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

long get_video_duration (AVFormatContext* fmt_ctx, AVStream* vid_stream) {

  long duration = vid_stream->duration;

  if (duration == AV_NOPTS_VALUE) {
    fprintf(stderr,
            "Video stream is omitting duration. Falling back to container "
            "duration.");

    duration =
        av_rescale_q(fmt_ctx->duration, AV_TIME_BASE_Q, vid_stream->time_base);
  }

  return duration;
}

long calculate_frame_steps (long duration, int segments) {
  long frame_steps = duration / segments;

  return frame_steps;
}

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

int main (int argc, char* argv[]) {  // NOLINT (unused-*)
  char* filename = (argc > 1) ? argv[1] : "./tulsi.mov";

  VideoReader vreader;

  // 1. Setup
  if (open_video_reader(filename, &vreader) < 0) {
    close_video_reader(&vreader);  // cleanup partial opens
    return -1;
  }

  AVStream* vid_stream_ptr = vreader.fmt_ctx->streams[vreader.video_stream_idx];
  AVFormatContext* format_context_ptr = vreader.fmt_ctx;
  long video_duration = get_video_duration(format_context_ptr, vid_stream_ptr);
  long frame_step = calculate_frame_steps(video_duration, 4);

  // 2. Loop through file packets

  /* We want to split the video into this many segments */
  int total_video_segments = 4;
  /* Counter for # of frames successfully */
  int frames_decoded = 0;
  /* Target timestamp */
  long target_timestamp = 0;
  for (int i = 0; i < total_video_segments; i++) {
    target_timestamp = ((long)i * frame_step);

    printf("\n--- Segment %d/%d : Seeking to TS %" PRId64 " ---\n", i + 1,
           total_video_segments, target_timestamp);

    // A. SEEK
    if (seek_to_timestamp(&vreader, target_timestamp) < 0) {
      fprintf(stderr, "Could not seek to segment %d\n", i);
      continue;  // Try next segment
    }

    // B. DECODE (Read packets until we get ONE valid frame)
    int frame_for_this_segment_found = 0;

    while (av_read_frame(vreader.fmt_ctx, vreader.packet) >= 0) {

      // Only process video packets
      if (vreader.packet->stream_index == vreader.video_stream_idx) {

        // We pass 'valid_frames_found' just for counting purposes
        // Note: decode_packet returns 0 on success, even if no frame ready yet
        int prev_frame_count = frames_decoded;

        if (decode_packet(&vreader, &frames_decoded) < 0) {
          break;  // Error
        }

        // Did valid_frames_found increment?
        // If yes, we got our frame for this segment!
        if (frames_decoded > prev_frame_count) {
          frame_for_this_segment_found = 1;
          av_packet_unref(vreader.packet);
          break;  // BREAK the inner read loop, move to next segment
        }
      }

      av_packet_unref(vreader.packet);
      if (!frame_for_this_segment_found) {
        printf("Warning: Could not extract frame for segment %d\n", i);
      }
    }
  }

  // 3. Cleanup
  close_video_reader(&vreader);

  printf("Done. Processed %d frames.\n", frames_decoded);
  return 0;
}
