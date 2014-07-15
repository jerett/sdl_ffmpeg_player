#include <libavcodec/avcodec.h>
#include <libavutil/avstring.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <stdio.h>

#include "packet_queue.h"
#include "constant.h"
#include "audio_handler.h"
#include "video_handler.h"
#include "videoState.h"

/* PacketQueue audioq; */
/* int quit = 0; */
SDL_Surface     *screen;
VideoState *global_video_state;

int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *pFormatCtx = is->pFormatCtx;
    AVCodecContext *codecCtx = NULL;
    AVCodec *codec = NULL;
    AVDictionary *optionsDict = NULL;
    SDL_AudioSpec wanted_spec, spec;

    if(stream_index < 0 || (unsigned int)stream_index >= pFormatCtx->nb_streams) {
        return -1;
    }

    // Get a pointer to the codec context for the video stream
    codecCtx = pFormatCtx->streams[stream_index]->codec;

    if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        // Set audio settings from codec info
        wanted_spec.freq = codecCtx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = codecCtx->channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = is;

        if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }
    }
    codec = avcodec_find_decoder(codecCtx->codec_id);
    if(!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    switch(codecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            is->audioStream = stream_index;
            is->audio_st = pFormatCtx->streams[stream_index];
            is->audio_buf_size = 0;
            is->audio_buf_index = 0;
            memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
            packet_queue_init(&is->audioq);
            SDL_PauseAudio(0);
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->videoStream = stream_index;
            is->video_st = pFormatCtx->streams[stream_index];

            packet_queue_init(&is->videoq);
            is->video_tid = SDL_CreateThread(video_thread, is);
            is->sws_ctx =
                sws_getContext
                (
                 is->video_st->codec->width,
                 is->video_st->codec->height,
                 is->video_st->codec->pix_fmt,
                 is->video_st->codec->width,
                 is->video_st->codec->height,
                 PIX_FMT_YUV420P,
                 SWS_BILINEAR,
                 NULL,
                 NULL,
                 NULL
                );
            break;
        default:
            break;
    }
    return 0;
}

int decode_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVFormatContext *pFormatCtx = NULL;
    AVPacket pkt1, *packet = &pkt1;

    int video_index = -1;
    int audio_index = -1;
    unsigned int i;

    is->videoStream=-1;
    is->audioStream=-1;
    // Open video file
    if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
        return -1; // Couldn't open file
    is->pFormatCtx = pFormatCtx;

    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0)
        return -1; // Couldn't find stream information

    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, is->filename, 0);

    // Find the first video stream

    for(i=0; i<pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
                video_index < 0) {
            video_index=i;
        }
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
                audio_index < 0) {
            audio_index=i;
        }
    }
    if(audio_index >= 0) {
        stream_component_open(is, audio_index);
    }
    if(video_index >= 0) {
        stream_component_open(is, video_index);
    }

    if(is->videoStream < 0 || is->audioStream < 0) {
        fprintf(stderr, "%s: could not open codecs\n", is->filename);
        goto fail;
    }

    // main decode loop
    while(1){
        if(is->quit) {
            break;
        }
        // seek stuff goes here
        if(is->audioq.size > MAX_AUDIOQ_SIZE ||
                is->videoq.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
        if(av_read_frame(is->pFormatCtx, packet) < 0) {
            if(is->pFormatCtx->pb->error == 0) {
                SDL_Delay(100); /* no error; wait for user input */
                continue;
            } else {
                break;
            }
        }
        // Is this a packet from the video stream?
        if(packet->stream_index == is->videoStream) {
            packet_queue_put(&is->videoq, packet);
        } else if(packet->stream_index == is->audioStream) {
            packet_queue_put(&is->audioq, packet);
            /* packet_queue_put(&audioq,packet); */
        } else {
            av_free_packet(packet);
        }
    }
    /* all done - wait for it */
    while(!is->quit) {
        SDL_Delay(100);
    }

fail:
    if(1){
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

void video_display(VideoState *is)
{
  SDL_Rect rect;
  VideoPicture *vp;
  //AVPicture pict;
  float aspect_ratio;
  int w, h, x, y;
  //int i;

  vp = &is->pictq[is->pictq_rindex];
  if(vp->bmp) {
    if(is->video_st->codec->sample_aspect_ratio.num == 0) {
      aspect_ratio = 0;
    } else {
      aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio) *
	is->video_st->codec->width / is->video_st->codec->height;
    }
    if(aspect_ratio <= 0.0) {
      aspect_ratio = (float)is->video_st->codec->width /
	(float)is->video_st->codec->height;
    }
    h = screen->h;
    w = ((int)rint(h * aspect_ratio)) & -3;
    if(w > screen->w) {
      w = screen->w;
      h = ((int)rint(w / aspect_ratio)) & -3;
    }
    x = (screen->w - w) / 2;
    y = (screen->h - h) / 2;

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_DisplayYUVOverlay(vp->bmp, &rect);
  }
}

void alloc_picture(void *userdata)
{
  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width,
				 is->video_st->codec->height,
				 SDL_YV12_OVERLAY,
				 screen);
  vp->width = is->video_st->codec->width;
  vp->height = is->video_st->codec->height;

  SDL_LockMutex(is->pictq_mutex);
  vp->allocated = 1;
  SDL_CondSignal(is->pictq_cond);
  SDL_UnlockMutex(is->pictq_mutex);
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque)
{
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}
void video_refresh_timer(void *userdata)
{
  VideoState *is = (VideoState *)userdata;
  // vp is used in later tutorials for synchronization
  //VideoPicture *vp;

  if(is->video_st) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      //vp = &is->pictq[is->pictq_rindex];
      /* Now, normally here goes a ton of code
	 about timing, etc. we're just going to
	 guess at a delay for now. You can
	 increase and decrease this value and hard code
	 the timing - but I don't suggest that ;)
	 We'll learn how to do it for real later.
      */
      schedule_refresh(is, 80);

      /* show the picture! */
      video_display(is);

      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}

int main(int argc, char *argv[])
{
    SDL_Event       event;
    VideoState      *is;

    is = (VideoState*)av_mallocz(sizeof(VideoState));
    global_video_state = is;

    if(argc < 2) {
        fprintf(stderr, "Usage: test <file>\n");
        exit(1);
    }
    // Register all formats and codecs
    av_register_all();
    avformat_network_init();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    // Make a screen to put our video
    screen = SDL_SetVideoMode(640, 480, 0, 0);
    if(!screen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        exit(1);
    }

    av_strlcpy(is->filename, argv[1], 1024);
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();

    schedule_refresh(is, 40);
    is->parse_tid = SDL_CreateThread(decode_thread, is);
    if(!is->parse_tid) {
        av_free(is);
        return -1;
    }
    while(1){
        SDL_WaitEvent(&event);
        switch(event.type) {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                is->quit = 1;
                /*
                 * If the video has finished playing, then both the picture and
                 * audio queues are waiting for more data.  Make them stop
                 * waiting and terminate normally.
                 */
                SDL_CondSignal(is->audioq.cond);
                SDL_CondSignal(is->videoq.cond);
                SDL_Quit();
                return 0;
                break;
            case FF_ALLOC_EVENT:
                alloc_picture(event.user.data1);
                break;
            case FF_REFRESH_EVENT:
                video_refresh_timer(event.user.data1);
                break;
            default:
                break;
        }
    }
    return 0;
}
