#include "mel_spectrum.h"

#include <math.h>
#include <float.h>

#include <liquid/liquid.h>

#include "logging.h"

#define _MEL_BREAK_FREQUENCY_HERTZ (700.0)
#define _MEL_HIGH_FREQUENCY_Q (1127.0)

struct _mel_spectrum_t
{
    size_t input_size;
    size_t output_size;
    smatrixf mel_mat;
    fftplan pf;
    float *w;
    float complex *x;
    float complex *X;
};

// Hann window
static float hann_(unsigned int _i,
                   unsigned int _wlen)
{
    // validate input
    log_assert(_i <= _wlen);

    // TODO test this function
    // TODO add reference
    return 0.5f - 0.5f * cosf((2 * M_PI * (float)_i) / ((float)(_wlen - 1)));
}

static inline float _hertz_to_mel(float frequencies_hertz)
{
    return _MEL_HIGH_FREQUENCY_Q * log(1.0 +
                                       (frequencies_hertz / _MEL_BREAK_FREQUENCY_HERTZ));
}

static smatrixf linear_to_mel_weight_matrix(size_t num_mel_bins,
                                            size_t num_spectrogram_bins,
                                            size_t sample_rate,
                                            float lower_edge_hertz,
                                            float upper_edge_hertz)
{
    size_t i, j;
    float start;
    float step;
    float spectrogram_bins_mel[num_spectrogram_bins];
    float band_edges_mel[num_mel_bins];

    log_assert(num_mel_bins > 0);
    log_assert(num_spectrogram_bins > 0);
    log_assert(sample_rate > 0);
    log_assert(lower_edge_hertz >= 0);
    log_assert(upper_edge_hertz > lower_edge_hertz);
    log_assert(upper_edge_hertz <= sample_rate / 2.0);

    smatrixf mat = smatrixf_create(num_mel_bins, num_spectrogram_bins);
    log_assert(mat);

    step = (float)sample_rate / (2 * (num_spectrogram_bins - 1));
    for (i = 0; i < num_spectrogram_bins - 1; i++)
    {
        spectrogram_bins_mel[i] = _hertz_to_mel((i + 1) * step);
    }

    start = _hertz_to_mel(lower_edge_hertz);
    step = (_hertz_to_mel(upper_edge_hertz) - start) / (num_mel_bins + 2 - 1);
    for (i = 0; i < num_mel_bins; i++)
    {
        band_edges_mel[i] = start + (i * step);
    }

    float lower_slope, upper_slope;

    for (i = 0; i < num_spectrogram_bins - 1; i++)
    {
        for (j = 0; j < num_mel_bins; j++)
        {
            float lower_edge_mel = band_edges_mel[j];
            float center_mel = lower_edge_mel + step;
            float upper_edge_mel = center_mel + step;
            lower_slope = (spectrogram_bins_mel[i] - lower_edge_mel) / (center_mel - lower_edge_mel);
            upper_slope = ((upper_edge_mel - spectrogram_bins_mel[i]) / (upper_edge_mel - center_mel));

            smatrixf_set(mat, j, i + 1, fmax(0.0, fmin(lower_slope, upper_slope)));
        }
    }
    return mat;
}

mel_spectrum_t *mel_spectrum_create(size_t input_size,
                                    size_t output_size,
                                    size_t sample_rate,
                                    float lower_edge_hertz,
                                    float upper_edge_hertz)
{
    mel_spectrum_t *self = (mel_spectrum_t *)malloc(sizeof(mel_spectrum_t));
    log_assert(self);

    self->mel_mat = linear_to_mel_weight_matrix(output_size,
                                                (input_size / 2) + 1,
                                                sample_rate,
                                                lower_edge_hertz,
                                                upper_edge_hertz);
    log_assert(self->mel_mat);

    self->input_size = input_size;
    self->output_size = output_size;

    self->w = malloc(input_size * sizeof(float));
    log_assert(self->w);

    self->x = malloc(input_size * sizeof(complex float));
    log_assert(self->x);

    self->X = malloc(input_size * sizeof(complex float));
    log_assert(self->X);

    for (size_t i = 0; i < input_size; i++)
    {
        self->w[i] = hann_(i, input_size);
    }

    self->pf = fft_create_plan(input_size, self->x, self->X, LIQUID_FFT_FORWARD, 0);
    log_assert(self->pf);

    return self;
}

void mel_spectrum_process(mel_spectrum_t *self, float const *input, float *output)
{
    size_t i;
    float tmp[(self->input_size / 2) + 1];

    for (i = 0; i < self->input_size; i++)
    {
        self->x[i] = (input[i] * self->w[i]) + (_Complex_I*(float)0.0);
    }

    fft_execute(self->pf);

    for (size_t i = 0; i < (self->input_size / 2) + 1; i++)
    {
        tmp[i] = cabsf(self->X[i]);
        tmp[i] *= tmp[i];
    }

    smatrixf_vmul(self->mel_mat, tmp, output);

    for (i = 0; i < self->output_size; i++)
    {
        output[i] = log(output[i] + FLT_MIN);
    }
}

void mel_spectrum_destroy(mel_spectrum_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        mel_spectrum_t *self = *self_p;
        free(self->w);
        free(self->x);
        free(self->X);
        smatrixf_destroy(self->mel_mat);
        fft_destroy_plan(self->pf);
        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}