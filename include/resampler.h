#ifndef __RESAMPLER_H__
#define __RESAMPLER_H__

#include <stdint.h>
#include <stddef.h>

#include "link.h"

typedef struct _resampler_t resampler_t;

resampler_t *resampler_create(unsigned int rate, unsigned int rrate, int offset, link_t *input);
link_t *resampler_get_output(resampler_t *self);
void resampler_destroy(resampler_t **self_p);

#endif // __RESAMPLER_H__
