#include "resampler.h"

#include <math.h>

#include <liquid/liquid.h>

#include "logging.h"

struct _resampler_t
{
    msresamp_crcf resamp;
    iirfilt_crcf dc_blocker;
    nco_crcf nco;
    void (*mix)(nco_crcf _q, float complex *_x, float complex *_y, unsigned int _n);

    link_t *output;
    int handle;
};

static bool resampler_handler(void *ctx, void *in_buf, const link_msg_t *in_msg,
                              void *out_buf, link_msg_t *out_msg)
{
    resampler_t *self = (resampler_t *)ctx;
    unsigned int n;

    if (self->nco)
    {
        self->mix(self->nco, (float complex *)in_buf, (float complex *)in_buf, in_msg->len);
    }

    msresamp_crcf_execute(self->resamp, (float complex *)in_buf, in_msg->len, (float complex *)out_buf, &n);
    out_msg->len = n;
    out_msg->id = 0;
    iirfilt_crcf_execute_block(self->dc_blocker, out_buf, out_msg->len, out_buf);

    return true;
}

resampler_t *resampler_create(unsigned int rate, unsigned int rrate, int offset, link_t *input)
{
    log_assert(rrate < rate);
    log_assert(abs(offset) < rate);
    log_assert((input->out_bs * rrate) % rate == 0);

    resampler_t *self = (resampler_t *)malloc(sizeof(resampler_t));
    log_assert(self);

    if (offset != 0)
    {
        self->nco = nco_crcf_create(LIQUID_VCO);
        log_assert(self->nco);

        float f = 2 * M_PI * ((float)offset / rate);
        if (f > 0)
        {
            nco_crcf_set_frequency(self->nco, f);
            self->mix = nco_crcf_mix_block_down;
        }
        else
        {
            nco_crcf_set_frequency(self->nco, -f);
            self->mix = nco_crcf_mix_block_up;
        }
    } else {
        self->nco = NULL;
    }

    self->resamp = msresamp_crcf_create((float)rrate / rate, 60.0f);
    log_assert(self->resamp);

    self->dc_blocker = iirfilt_crcf_create_dc_blocker(0.0005);
    log_assert(self->dc_blocker);

    self->output = link_connect("resampler", input, 2,
                                input->out_bs, sizeof(complex float),
                                (input->out_bs * rrate) / rate, sizeof(complex float));
    log_assert(self->output);

    self->handle = go(link_run(self->output, self, resampler_handler));
    log_assert(self->handle >= 0);

    return self;
}

link_t *resampler_get_output(resampler_t *self)
{
    return self->output;
}

void resampler_destroy(resampler_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        resampler_t *self = *self_p;
        int ret = hclose(self->handle);
        log_assert(ret == 0);
        msresamp_crcf_destroy(self->resamp);
        if (self->nco)
        {
            nco_crcf_destroy(self->nco);
        }
        iirfilt_crcf_destroy(self->dc_blocker);
        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}
