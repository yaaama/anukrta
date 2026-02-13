#ifndef VIDEO_H_
#define VIDEO_H_

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum number of video segments to process */
#define ANU_MAX_VIDEO_SEGMENTS 20

typedef struct video_io {
  /* File (container/AV file) context
   * AVFormatContext holds the header information stored in file (container) */
  AVFormatContext *fmt_ctx;
  /* Video encoding context.
     Codec is used to decode the video stream */
  AVCodecContext *codec_ctx;

  /* Packet (compressed frame of audio/video) */
  AVPacket *packet;
  /* Decoded packet */
  AVFrame *frame;

  /* Index of video stream inside container */
  int video_stream_idx;
  /* Video duration in microseconds */
  long video_duration;

} video_io;

AVStream *vreader_get_video_stream(video_io *vreader);

int open_video_reader(char *filename, video_io *vreader);
void close_video_reader(video_io *vreader);
long get_video_duration(video_io *vreader);
int seek_to_timestamp(video_io *vreader, int64_t target_pts);
int init_grey_frame(int width, int height, AVFrame *out_frame);
int scale_frame(AVFrame *src_frame, size_t width, size_t height,
                AVFrame *out_frame);
int decode_packet(video_io *vreader);
#endif  // VIDEO_H_
