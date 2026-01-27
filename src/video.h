#ifndef VIDEO_H_
#define VIDEO_H_

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <stddef.h>
#include <stdint.h>
/* Maximum number of video segments to process */
#define ANU_MAX_VIDEO_SEGMENTS 20

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

int open_video_reader(char* filename, VideoReader* vreader);
void close_video_reader(VideoReader* vreader);
long get_video_duration(AVFormatContext* fmt_ctx, AVStream* vid_stream);
int seek_to_timestamp(VideoReader* vreader, int64_t target_pts);
int init_gray_frame(int width, int height, AVFrame* out_frame);
int scale_frame(AVFrame* src_frame, size_t width, size_t height,
                AVFrame* out_frame);
int decode_packet(VideoReader* vreader);
#endif  // VIDEO_H_
