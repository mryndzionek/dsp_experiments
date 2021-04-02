#ifndef __SOAPY_SOURCE_H__
#define __SOAPY_SOURCE_H__

#include <libwebsockets.h>
#include "link.h"

typedef struct _soapy_source_t soapy_source_t;

soapy_source_t *soapy_source_create(double samplerate, double frequency, link_t *output);
void soapy_source_start(soapy_source_t *self);
void soapy_source_set_frequency(soapy_source_t *self, double frequency);
void soapy_source_destroy(soapy_source_t **self_p);

#endif // __SOAPY_SOURCE_H__
