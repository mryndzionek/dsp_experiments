#ifndef __AUDIO_SINK_H__
#define __AUDIO_SINK_H__

#include <libwebsockets.h>
#include "link.h"

typedef struct _audio_sink_t audio_sink_t;

audio_sink_t *audio_sink_create(unsigned int samplerate, unsigned int num_channels,
                                link_t *input);
void audio_sink_destroy(audio_sink_t **self_p);

#endif // __AUDIO_SINK_H__
