#ifndef _VIDEO_STATE_H
#define _VIDEO_STATE_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "packet_queue.h"
#include "constant.h"
#include "audio_handler.h"
#include "video_handler.h"


typedef struct VideoState {
  AVFormatContext *pFormatCtx;
  int             videoStream, audioStream;
  AVStream        *audio_st;
  PacketQueue     audioq;
  uint8_t         audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
  unsigned int    audio_buf_size;
  unsigned int    audio_buf_index;
  AVFrame         audio_frame;
  AVPacket        audio_pkt;
  uint8_t         *audio_pkt_data;
  int             audio_pkt_size;
  AVStream        *video_st;
  PacketQueue     videoq;

  VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
  int             pictq_size, pictq_rindex, pictq_windex;
  SDL_mutex       *pictq_mutex;
  SDL_cond        *pictq_cond;

  SDL_Thread      *parse_tid;
  SDL_Thread      *video_tid;

  char            filename[1024];
  int             quit;

  AVIOContext     *io_context;
  struct SwsContext *sws_ctx;

  double video_clock;
  double audio_clock;
  double frame_last_delay;
  double frame_last_pts;
  double frame_timer;
} VideoState;


#endif
