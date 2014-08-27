#ifndef _AUDIO_HANDLER_H
#define _AUDIO_HANDLER_H

// #include <SDL/SDL.h>
#include "videoState.h"
struct VideoState;
void audio_callback(void *userdata, Uint8 *stream, int len);
double get_audio_clock(struct VideoState *is);

#endif
