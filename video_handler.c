#include "video_handler.h"
#include "videoState.h"
#include <libavformat/avformat.h>

int queue_picture(VideoState *is, AVFrame *pFrame)
{
    VideoPicture *vp;
    AVPicture pict;

    /* wait until we have space for a new pic */
    SDL_LockMutex(is->pictq_mutex);
    while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
            !is->quit) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);

    if(is->quit)
        return -1;

    // windex is set to 0 initially
    vp = &is->pictq[is->pictq_windex];

    /* allocate or resize the buffer! */
    if(!vp->bmp ||
            vp->width != is->video_st->codec->width ||
            vp->height != is->video_st->codec->height) {
        SDL_Event event;

        vp->allocated = 0;
        /* we have to do it in the main thread */
        event.type = FF_ALLOC_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);

        /* wait until we have a picture allocated */
        SDL_LockMutex(is->pictq_mutex);
        while(!vp->allocated && !is->quit) {
            SDL_CondWait(is->pictq_cond, is->pictq_mutex);
        }
        SDL_UnlockMutex(is->pictq_mutex);
        if(is->quit) {
            return -1;
        }
    }
    /* We have a place to put our picture on the queue */

    if(vp->bmp) {

        SDL_LockYUVOverlay(vp->bmp);

        /* point pict at the queue */

        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];

        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];

        // Convert the image into YUV format that SDL uses
        sws_scale
            (
             is->sws_ctx,
             (uint8_t const * const *)pFrame->data,
             pFrame->linesize,
             0,
             is->video_st->codec->height,
             pict.data,
             pict.linesize
            );

        SDL_UnlockYUVOverlay(vp->bmp);
        /* now we inform our display thread that we have a pic ready */
        if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
            is->pictq_windex = 0;
        }
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

int video_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;
    int frameFinished;
    AVFrame *pFrame;

    pFrame = av_frame_alloc();

    for(;;) {
        if(packet_queue_get(&is->videoq, packet, 1) < 0) {
            // means we quit getting packets
            break;
        }
        // Decode video frame
        avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished,
                packet);

        // Did we get a video frame?
        if(frameFinished) {
            if(queue_picture(is, pFrame) < 0) {
                break;
            }
        }
        av_free_packet(packet);
    }
    av_free(pFrame);
    return 0;
}

