#ifndef _VIDEO_HANDLER_H
#define _VIDEO_HANDLER_H

#include <SDL/SDL.h>

typedef struct VideoPicture {
  SDL_Overlay *bmp;
  int width, height; /* source height & width */
  int allocated;
} VideoPicture;

int video_thread(void *arg);

#endif
