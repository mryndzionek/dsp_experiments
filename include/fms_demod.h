#ifndef __FMS_DEMOD_H__
#define __FMS_DEMOD_H__

#include <stdint.h>
#include <stddef.h>

#include "link.h"

typedef struct _fms_demod_t fms_demod_t;

fms_demod_t *fms_demod_create(unsigned int rate, unsigned int decim, link_t *input);
link_t *fms_demod_get_output(fms_demod_t *self);
void fms_demod_destroy(fms_demod_t **self_p);

#endif // __FMS_DEMOD_H__