#ifndef __MEL_SPECTRUM_H__
#define __MEL_SPECTRUM_H__

#include <stdint.h>
#include <stddef.h>

typedef struct _mel_spectrum_t mel_spectrum_t;

mel_spectrum_t *mel_spectrum_create(size_t input_size,
                                    size_t output_size,
                                    size_t sample_rate,
                                    float lower_edge_hertz,
                                    float upper_edge_hertz);
void mel_spectrum_process(mel_spectrum_t *self, float const *input, float *output);
void mel_spectrum_destroy(mel_spectrum_t **self_p);

#endif // __MEL_SPECTRUM_H__