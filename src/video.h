#ifndef ANU_VIDEO_H
#define ANU_VIDEO_H

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

AVStream *anu_video_get_vid_stream(video_io *vreader);

int anu_video_open(char *filename, video_io *vreader);
void anu_video_close(video_io *vreader);
long anu_video_get_duration(video_io *vreader);
int anu_video_seek_to_timestamp_pts(video_io *vreader, int64_t target_pts);
int anu_video_frame_init(int width, int height, AVFrame *out_frame);
int anu_video_scale_frame(AVFrame *src_frame, size_t width, size_t height,
                          AVFrame *out_frame);
int anu_video_decode_packet(video_io *vreader);
void copy_frame_to_buffer(AVFrame *frame, uint8_t *dest, int width);
double anu_time_microseconds_to_seconds(long microseconds);
long anu_time_seconds_to_microseconds(double seconds);

#endif  // ANU_VIDEO_H
