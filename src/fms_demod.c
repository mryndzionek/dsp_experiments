#include "fms_demod.h"

#include <math.h>

#include <liquid/liquid.h>

#include "logging.h"

#define PILOT_FREQ_HZ (19000.0f)
#define PLL_BANDWIDTH_HZ (9.0f)
#define PILOT_FIR_HALFBAND_HZ (800.0f)
#define AUDIO_FIR_CUTOFF_HZ (15000.0f)
#define DEEMPHASIS_ORDER (2)
#define DEEMPHASIS_CUTOFF_HZ (5000.0f)
#define STEREO_GAIN (2.0f)

struct _fms_demod_t
{
    freqdem fmdemod;
    nco_crcf nco_pilot_approx;
    nco_crcf nco_pilot_exact;
    nco_crcf nco_stereo_subcarrier;
    firfilt_crcf fir_pilot;
    wdelaycf audio_delay;
    firfilt_crcf fir_l_plus_r;
    firfilt_crcf fir_l_minus_r;
    iirfilt_crcf iir_deemph_l;
    iirfilt_crcf iir_deemph_r;
    firdecim_rrrf fir_decim_l;
    firdecim_rrrf fir_decim_r;

    int handle;
    link_t *output;
    unsigned int decim;
};

static bool fms_demod_handle(void *ctx, void *in_buf, const link_msg_t *in_msg,
                             void *out_buf, link_msg_t *out_msg)
{
    fms_demod_t *self = (fms_demod_t *)ctx;
    float tmp_l[self->decim];
    float tmp_r[self->decim];
    size_t j = 0, k = 0;

    out_msg->id = 0;
    out_msg->len = 2 * (in_msg->len / self->decim);
    float tmp[in_msg->len];

    freqdem_demodulate_block(self->fmdemod, (float complex *)in_buf, in_msg->len, tmp);

    for (size_t i = 0; i < in_msg->len; i++)
    {
        complex float s = ((float *)tmp)[i] + 0.0 * I;
        complex float t, p;

        wdelaycf_push(self->audio_delay, s);
        nco_crcf_mix_down(self->nco_pilot_approx, s, &t);
        firfilt_crcf_push(self->fir_pilot, t);
        firfilt_crcf_execute(self->fir_pilot, &t);
        nco_crcf_mix_up(self->nco_pilot_approx, t, &p);
        nco_crcf_step(self->nco_pilot_approx);

        nco_crcf_set_phase(self->nco_stereo_subcarrier, 2.0 * nco_crcf_get_phase(self->nco_pilot_exact));

        nco_crcf_cexpf(self->nco_pilot_exact, &t);
        float phase_err = cargf(p * conjf(t));
        nco_crcf_pll_step(self->nco_pilot_exact, phase_err);
        nco_crcf_step(self->nco_pilot_exact);

        wdelaycf_read(self->audio_delay, &t);
        firfilt_crcf_push(self->fir_l_plus_r, t);
        nco_crcf_mix_down(self->nco_stereo_subcarrier, t, &p);
        firfilt_crcf_push(self->fir_l_minus_r, p);
        firfilt_crcf_execute(self->fir_l_plus_r, &t);
        firfilt_crcf_execute(self->fir_l_minus_r, &p);

        float lpr = crealf(t);
        float lmr = STEREO_GAIN * creal(p);
        float left = lpr + lmr;
        float right = lpr - lmr;

        iirfilt_crcf_execute(self->iir_deemph_l, (left + 0.0f * I), &t);
        iirfilt_crcf_execute(self->iir_deemph_r, (right + 0.0f * I), &p);

        tmp_l[j] = creal(t);
        tmp_r[j++] = creal(p);

        if (j == self->decim)
        {
            j = 0;
            firdecim_rrrf_execute(self->fir_decim_l, tmp_l, &((float *)out_buf)[2 * k]);
            firdecim_rrrf_execute(self->fir_decim_r, tmp_r, &((float *)out_buf)[(2 * k) + 1]);
            k++;
        }
    }

    return true;
}

static firfilt_crcf create_fir_filter(unsigned int len, float fc, float As, float mu)
{
    firfilt_crcf f = firfilt_crcf_create_kaiser(len, fc, As, mu);
    log_assert(f);
    firfilt_crcf_set_scale(f, 2.0f * fc);

    return f;
}

fms_demod_t *fms_demod_create(unsigned int rate, unsigned int decim, link_t *input)
{
    if (rate < 106000)
    {
        LOG(ERROR, "Rate must be >= 106000");
        return NULL;
    }

    fms_demod_t *self = (fms_demod_t *)malloc(sizeof(fms_demod_t));
    log_assert(self);
    self->decim = decim;

    self->fmdemod = freqdem_create(0.8);
    log_assert(self->fmdemod);

    self->nco_pilot_approx = nco_crcf_create(LIQUID_VCO);
    log_assert(self->nco_pilot_approx);
    nco_crcf_set_frequency(self->nco_pilot_approx, PILOT_FREQ_HZ * 2 * M_PI / rate);

    self->nco_pilot_exact = nco_crcf_create(LIQUID_VCO);
    log_assert(self->nco_pilot_exact);
    nco_crcf_set_frequency(self->nco_pilot_exact, PILOT_FREQ_HZ * 2 * M_PI / rate);
    nco_crcf_pll_set_bandwidth(self->nco_pilot_exact, PLL_BANDWIDTH_HZ / rate);

    self->nco_stereo_subcarrier = nco_crcf_create(LIQUID_VCO);
    log_assert(self->nco_stereo_subcarrier);
    nco_crcf_set_frequency(self->nco_stereo_subcarrier, 2 * PILOT_FREQ_HZ * 2 * M_PI / rate);

    self->fir_pilot = create_fir_filter(rate / 1350.0, PILOT_FIR_HALFBAND_HZ / rate, 60.0, 0.0);
    log_assert(self->fir_pilot);

    self->audio_delay = wdelaycf_create(firfilt_crcf_groupdelay(self->fir_pilot, 100.0 / rate));
    log_assert(self->audio_delay);

    self->fir_l_plus_r = create_fir_filter(rate / 1350.0, AUDIO_FIR_CUTOFF_HZ / rate, 60.0, 0.0);
    log_assert(self->fir_l_plus_r);

    self->fir_l_minus_r = create_fir_filter(rate / 1350.0, AUDIO_FIR_CUTOFF_HZ / rate, 60.0, 0.0);
    log_assert(self->fir_l_minus_r);

    self->iir_deemph_l = iirfilt_crcf_create_prototype(LIQUID_IIRDES_BUTTER,
                                                       LIQUID_IIRDES_LOWPASS,
                                                       LIQUID_IIRDES_SOS,
                                                       DEEMPHASIS_ORDER, DEEMPHASIS_CUTOFF_HZ / rate,
                                                       0.0, 10.0, 10.0);
    log_assert(self->iir_deemph_l);

    self->iir_deemph_r = iirfilt_crcf_create_prototype(LIQUID_IIRDES_BUTTER,
                                                       LIQUID_IIRDES_LOWPASS,
                                                       LIQUID_IIRDES_SOS,
                                                       DEEMPHASIS_ORDER, DEEMPHASIS_CUTOFF_HZ / rate,
                                                       0.0, 10.0, 10.0);
    log_assert(self->iir_deemph_r);

    self->fir_decim_l = firdecim_rrrf_create_kaiser(decim, 10, 60.0);
    log_assert(self->fir_decim_l);

    self->fir_decim_r = firdecim_rrrf_create_kaiser(decim, 10, 60.0);
    log_assert(self->fir_decim_r);

    self->output = link_connect("fms_demod", input, 2,
                                input->out_bs, sizeof(complex float),
                                2 * (input->out_bs / decim), sizeof(float));
    log_assert(self->output);

    self->handle = go(link_run(self->output, self, fms_demod_handle));
    log_assert(self->handle >= 0);

    return self;
}

link_t *fms_demod_get_output(fms_demod_t *self)
{
    return self->output;
}

void fms_demod_destroy(fms_demod_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        fms_demod_t *self = *self_p;
        int ret = hclose(self->handle);
        log_assert(ret == 0);
        iirfilt_crcf_destroy(self->iir_deemph_r);
        iirfilt_crcf_destroy(self->iir_deemph_l);
        firfilt_crcf_destroy(self->fir_l_minus_r);
        firfilt_crcf_destroy(self->fir_l_plus_r);
        wdelaycf_destroy(self->audio_delay);
        firfilt_crcf_destroy(self->fir_pilot);
        nco_crcf_destroy(self->nco_stereo_subcarrier);
        nco_crcf_destroy(self->nco_pilot_exact);
        nco_crcf_destroy(self->nco_pilot_approx);
        freqdem_destroy(self->fmdemod);

        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}
