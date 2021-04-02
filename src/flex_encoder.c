#include "flex_encoder.h"

#include <complex.h>
#include <math.h>

#include <liquid/liquid.h>
#include <libdill.h>

#include "logging.h"

#define INTERP (10)
#define MAX_PAYLOAD_SIZE (480)
#define SAMPLERATE (48000)
#define BLOCK_SIZE (2000)

struct _flex_encoder_t
{
    flexframegenprops_s fgprops;
    flexframegen fg;
    msresamp_crcf resamp;
    nco_crcf nco;
    firhilbf fh;
    unsigned char header[14];
    size_t bs;
    link_t *output;
    int handle;
};

static bool flex_encoder_handler(void *ctx, void *in_buf, const link_msg_t *in_msg,
                                 void *out_buf, link_msg_t *out_msg)
{
    flex_encoder_t *self = (flex_encoder_t *)ctx;
    int frame_complete = 0;
    static bool idle = true;
    unsigned int n;
    complex float tmp[self->bs];

    if (idle)
    {
        flexframegen_reset(self->fg);
        flexframegen_setprops(self->fg, &self->fgprops);
        //flexframegen_print(self->fg);
        flexframegen_assemble(self->fg, self->header, in_buf, in_msg->len);
        idle = false;
    }

    frame_complete = flexframegen_write_samples(self->fg, tmp, self->bs);
    msresamp_crcf_execute(self->resamp, tmp, self->bs, out_buf, &n);
    LOG(DEBUG, "frame_complete: %d %d", frame_complete, n);

    nco_crcf_mix_block_down(self->nco, out_buf, out_buf, n);
    firhilbf_interp_execute_block(self->fh, out_buf, n, out_buf);

    out_msg->len = 2 * n;

    if (frame_complete)
    {
        idle = true;
    }

    return frame_complete;
}

flex_encoder_t *flex_encoder_create(link_t *input)
{
    flex_encoder_t *self = (flex_encoder_t *)malloc(sizeof(flex_encoder_t));
    log_assert(self);

    flexframegenprops_init_default(&self->fgprops);
    self->fgprops.mod_scheme = LIQUID_MODEM_QPSK;
    self->fgprops.check = LIQUID_CRC_32;
    self->fgprops.fec0 = LIQUID_FEC_NONE;
    self->fgprops.fec1 = LIQUID_FEC_NONE;
    self->fg = flexframegen_create(&self->fgprops);
    log_assert(self->fg);

    self->resamp = msresamp_crcf_create(1.0f * INTERP, 60.0f);
    log_assert(self->resamp);

    self->nco = nco_crcf_create(LIQUID_VCO);
    log_assert(self->nco);

    float f = 2 * M_PI * 0.1f;
    nco_crcf_set_frequency(self->nco, f);

    self->fh = firhilbf_create(5, 60.0f);
    log_assert(self->fh);

    for (size_t i = 0; i < sizeof(self->header); i++)
        self->header[i] = i;

    self->output = link_connect("flex_encoder", input, 2,
                                input->out_bs, sizeof(uint8_t),
                                2000, sizeof(float));
    log_assert(self->output);
    log_assert(self->output->out_bs % (2 * INTERP) == 0);
    self->bs = self->output->out_bs / (2 * INTERP);
    self->output->async = true;

    self->handle = go(link_run(self->output, self, flex_encoder_handler));
    log_assert(self->handle >= 0);

    return self;
}

link_t *flex_encoder_get_output(flex_encoder_t *self)
{
    return self->output;
}

void flex_encoder_destroy(flex_encoder_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        int ret;
        flex_encoder_t *self = *self_p;

        ret = hclose(self->handle);
        log_assert(ret == 0);

        flexframegen_destroy(self->fg);
        msresamp_crcf_destroy(self->resamp);
        nco_crcf_destroy(self->nco);
        firhilbf_destroy(self->fh);

        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}
