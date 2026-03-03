#ifndef ANU_VIDEO_H
#define ANU_VIDEO_H

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <stddef.h>
#include <stdint.h>

#include "explore.h"
#include "util.h"

/* Maximum number of video segments to process */
#define ANU_MAX_VIDEO_SEGMENTS 20

typedef struct anu_vreader {
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

  /* Index of video stream inside container */
  int video_stream_idx;
  /* Video duration in microseconds */
  long video_duration;

} anu_vreader;

int anu_video_hash(anu_file *file, anukrta_config *config,
                   uint64_t *hashes_out);

#endif  // ANU_VIDEO_H
