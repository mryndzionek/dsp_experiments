#ifndef __WBFM_DEMOD_H__
#define __WBFM_DEMOD_H__

#include <stdint.h>
#include <stddef.h>

#include "link.h"

typedef struct _wbfm_demod_t wbfm_demod_t;

wbfm_demod_t *wbfm_demod_create(unsigned int rate, unsigned int decim, link_t *input);
link_t *wbfm_demod_get_output(wbfm_demod_t *self);
void wbfm_demod_destroy(wbfm_demod_t **self_p);

#endif // __WBFM_DEMOD_H__