#ifndef AK_VIDEO_H
#define AK_VIDEO_H

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "signals.h"
#include "util.h"

typedef struct ak_vreader {
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
  char *fname;
  /* Index of video stream inside container */
  int video_stream_idx;
} ak_vreader;

enum AK_FLAG_ENUM : u32 {
  AK_LAV_SUPPS_DEC_GRAY = (1 << 0)
};

/**
 * Checks if the linked libavcodec was compiled with certain features.
 *
 * @return Supported features bitflag (`AK_LAV_SUPPS_`)
 */
u32 ak_libav_supports(void);

AK_STATUS ak_video_hash(ak_file *file,
                        ak_config *config,
                        ak_signals_ctx *signals,
                        ak_hash_entry *entries_out);

#endif  // AK_VIDEO_H
