#include "wbfm_demod.h"

#include <liquid/liquid.h>

#include "logging.h"

struct _wbfm_demod_t
{
    firdecim_rrrf fir_decim;
    iirfilt_rrrf iir_deemph;
    freqdem fmdemod;
    link_t *output;
    int handle;
    unsigned int decim;
};

static bool wbfm_demod_handler(void *ctx, void *in_buf, const link_msg_t *in_msg,
                               void *out_buf, link_msg_t *out_msg)
{
    wbfm_demod_t *self = (wbfm_demod_t*)ctx;
    float tmp[in_msg->len];

    freqdem_demodulate_block(self->fmdemod, (float complex *)in_buf, in_msg->len, tmp);
    iirfilt_rrrf_execute_block(self->iir_deemph, tmp, in_msg->len, tmp);

    out_msg->len = in_msg->len / self->decim;
    out_msg->id = 0; 
    firdecim_rrrf_execute_block(self->fir_decim, tmp, out_msg->len, (float *)out_buf);

    return true;
}

wbfm_demod_t *wbfm_demod_create(unsigned int rate, unsigned int decim, link_t *input)
{
    log_assert((rate % decim) == 0);
    firdecim_rrrf firdecim = firdecim_rrrf_create_kaiser(decim, 10, 60.0);
    log_assert(firdecim);
    //firdecim_rrrf_print(firdecim);

    iirfilt_rrrf iir_deemph = iirfilt_rrrf_create_prototype(LIQUID_IIRDES_BUTTER,
                                                            LIQUID_IIRDES_LOWPASS,
                                                            LIQUID_IIRDES_SOS,
                                                            2, 5000.0 / rate, 0.0, 10.0, 10.0);
    log_assert(iir_deemph);
    //iirfilt_rrrf_print(iir_deemph);

    freqdem fmdemod = freqdem_create(0.6);
    log_assert(fmdemod);
    //freqdem_print(fmdemod);

    wbfm_demod_t *self = (wbfm_demod_t *)malloc(sizeof(wbfm_demod_t));
    log_assert(self);

    self->output = link_connect("wbfm_demod", input, 2,
                                input->out_bs, sizeof(complex float),
                                (input->out_bs / decim), sizeof(float));
    log_assert(self->output);

    self->fir_decim = firdecim;
    self->iir_deemph = iir_deemph;
    self->fmdemod = fmdemod;
    self->decim = decim;

    self->handle = go(link_run(self->output, self, wbfm_demod_handler));
    log_assert(self->handle >= 0);

    return self;
}

link_t *wbfm_demod_get_output(wbfm_demod_t *self)
{
    return self->output;
}

void wbfm_demod_destroy(wbfm_demod_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        wbfm_demod_t *self = *self_p;
        int ret = hclose(self->handle);
        log_assert(ret == 0);
        freqdem_destroy(self->fmdemod);
        iirfilt_rrrf_destroy(self->iir_deemph);
        firdecim_rrrf_destroy(self->fir_decim);
        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}