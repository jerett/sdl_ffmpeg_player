#ifndef _PACKET_QUEUE_H
#define _PACKET_QUEUE_H
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <libavformat/avformat.h>

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;


void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q,AVPacket *pkt);
int packet_queue_get(PacketQueue *q,AVPacket *pkt,int block);

#endif
