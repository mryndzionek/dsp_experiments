#ifndef __FLEX_ENCODER_H__
#define __FLEX_ENCODER_H__

#include <libwebsockets.h>
#include "link.h"

typedef struct _flex_encoder_t flex_encoder_t;

flex_encoder_t *flex_encoder_create(link_t *input);
link_t *flex_encoder_get_output(flex_encoder_t *self);
void flex_encoder_destroy(flex_encoder_t **self_p);

#endif // __FLEX_ENCODER_H__
