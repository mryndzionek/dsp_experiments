#ifndef __AUDIO_SOURCE_H__
#define __AUDIO_SOURCE_H__

#include <libwebsockets.h>
#include "link.h"

typedef struct _audio_source_t audio_source_t;

audio_source_t *audio_source_create(unsigned int samplerate);
void audio_source_start(audio_source_t *self);
link_t *audio_source_get_output(audio_source_t *self);
void audio_source_destroy(audio_source_t **self_p);

#endif // __AUDIO_SOURCE_H__
