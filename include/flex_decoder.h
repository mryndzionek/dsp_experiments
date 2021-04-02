#ifndef __FLEX_DECODER_H__
#define __FLEX_DECODER_H__

#include <stdint.h>
#include <stddef.h>

#include "link.h"

typedef struct _flex_decoder_t flex_decoder_t;

flex_decoder_t *flex_decoder_create(link_t *input);
link_t *flex_decoder_get_output(flex_decoder_t *self);
void flex_decoder_destroy(flex_decoder_t **self_p);

#endif // __FLEX_DECODER_H__