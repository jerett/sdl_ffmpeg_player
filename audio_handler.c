#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include "audio_handler.h"
#include "videoState.h"
#include "packet_queue.h"
#include "constant.h"

int AudioResampling(AVCodecContext * audio_dec_ctx,
        AVFrame * pAudioDecodeFrame,
        enum AVSampleFormat out_sample_fmt,
        int out_channels,
        int out_sample_rate,
        uint8_t* out_buf)
{
    struct SwrContext * swr_ctx = 0;
    swr_ctx = swr_alloc_set_opts(swr_ctx,audio_dec_ctx->channel_layout,out_sample_fmt,out_sample_rate,
            audio_dec_ctx->channel_layout,audio_dec_ctx->sample_fmt,audio_dec_ctx->sample_rate,0,0);
    int ret = 0;
    int dst_linesize = 0;
    int resampled_data_size = av_samples_get_buffer_size(&dst_linesize, out_channels,audio_dec_ctx->frame_size,audio_dec_ctx->sample_fmt, 1);
    uint8_t *dst_data = (uint8_t*)av_malloc(resampled_data_size);

    if ((ret = swr_init(swr_ctx)) < 0) {
        printf("Failed to initialize the resampling context\n");
        return -1;
    }

    if (swr_ctx){
        ret = swr_convert(swr_ctx, &dst_data, dst_linesize,
                (const uint8_t **)pAudioDecodeFrame->data, pAudioDecodeFrame->nb_samples);
        resampled_data_size = av_samples_get_buffer_size(&dst_linesize,out_channels,ret,out_sample_fmt,1);
        if (ret < 0) {
            printf("swr_convert error \n");
            return -1;
        }

        if (resampled_data_size < 0) {
            printf("av_samples_get_buffer_size error \n");
            return -1;
        }
    } else{
        printf("swr_ctx null error \n");
        return -1;
    }

    memcpy(out_buf, dst_data, resampled_data_size);

    if (dst_data) {
        av_free(dst_data);
    }

    if (swr_ctx)
    {
        swr_free(&swr_ctx);
    }
    return resampled_data_size;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size,double *pts_ptr)
{
    AVCodecContext *aCodecCtx = is->audio_st->codec;
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;
    double pts;

    for(;;) {
        while(audio_pkt_size > 0) {
            int got_frame = 0;
            len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
            if(len1 < 0) {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            data_size = AudioResampling(aCodecCtx,&frame, AV_SAMPLE_FMT_S16, 2, 44100, audio_buf);
            audio_pkt_data += len1;
            audio_pkt_size -= len1;

            if(data_size <= 0) {
                continue;
            }
            pts = is->audio_clock;
            *pts_ptr = pts;
            int n = 2 * is->audio_st->codec->channels;
            is->audio_clock += (double)data_size /
                (double)(n * is->audio_st->codec->sample_rate);
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if(pkt.data)
            av_free_packet(&pkt);

        if(is->quit) {
            return -1;
        }

        if(packet_queue_get(&is->audioq, &pkt, 1) < 0) {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;

        if(pkt.pts != AV_NOPTS_VALUE){
            is->audio_clock = av_q2d(is->audio_st->time_base)*pkt.pts;
        }
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{
    VideoState *is = (VideoState*)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    double pts;
    while(len > 0) {
        if(audio_buf_index >= audio_buf_size) {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(is, audio_buf, audio_buf_size,&pts);
            if(audio_size < 0) {
                /* If error, output silence */
                audio_buf_size = 1024; // arbitrary?
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

