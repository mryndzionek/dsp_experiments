#include "flex_decoder.h"

#include <string.h>
#include <math.h>

#include <liquid/liquid.h>

#include "logging.h"

#define INTERP (10)

struct _flex_decoder_t
{
    flexframesync fs;
    msresamp_crcf resamp;
    nco_crcf nco;
    firhilbf fh;
    link_t *output;
    bool frame_detected;
    unsigned char *payload_p;
    unsigned int payload_len;
    int handle;
};

static int callback(unsigned char *_header,
                    int _header_valid,
                    unsigned char *_payload,
                    unsigned int _payload_len,
                    int _payload_valid,
                    framesyncstats_s _stats,
                    void *_userdata)
{
    flex_decoder_t *self = (flex_decoder_t *)_userdata;
    //framesyncstats_print(&_stats);

    if(_header_valid && _payload_valid)
    {
        self->frame_detected = true;
        self->payload_len = _payload_len;
        memcpy(self->payload_p, _payload, _payload_len);
    }

    return 0;
}

static bool flex_decoder_handler(void *ctx, void *in_buf, const link_msg_t *in_msg,
                                 void *out_buf, link_msg_t *out_msg)
{
    unsigned int n;
    flex_decoder_t *self = (flex_decoder_t *)ctx;
    complex float tmp1[in_msg->len / 2];
    complex float tmp2[in_msg->len / (2 * INTERP)];

    firhilbf_decim_execute_block(self->fh, (float *)in_buf, in_msg->len / 2, tmp1);

    nco_crcf_mix_block_up(self->nco, tmp1, tmp1, in_msg->len / 2);
    msresamp_crcf_execute(self->resamp, tmp1, in_msg->len / 2, tmp2, &n);

    self->frame_detected = false;
    self->payload_p = out_buf;
    flexframesync_execute(self->fs, tmp2, n);
    if(self->frame_detected)
    {
        LOG(DEBUG, "Flex frame detected (len: %u)", self->payload_len);
        out_msg->len = self->payload_len;
    }

    return true;
}

flex_decoder_t *flex_decoder_create(link_t *input)
{
    flex_decoder_t *self = (flex_decoder_t *)malloc(sizeof(flex_decoder_t));
    log_assert(self);

    self->fs = flexframesync_create(callback, self);
    //flexframesync_debug_enable(self->fs);

    self->resamp = msresamp_crcf_create(1.0f / INTERP, 60.0f);
    log_assert(self->resamp);

    self->nco = nco_crcf_create(LIQUID_VCO);
    log_assert(self->nco);

    float f = 2 * M_PI * 0.1f;
    nco_crcf_set_frequency(self->nco, f);

    self->fh = firhilbf_create(5, 60.0f);
    log_assert(self->fh);

    self->output = link_connect("flex_decoder", input, 4,
                                input->out_bs, sizeof(float),
                                1024, sizeof(char));
    log_assert(self->output);

    self->handle = go(link_run(self->output, self, flex_decoder_handler));
    log_assert(self->handle >= 0);

    return self;
}

link_t *flex_decoder_get_output(flex_decoder_t *self)
{
    return self->output;
}

void flex_decoder_destroy(flex_decoder_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        flex_decoder_t *self = *self_p;
        int ret = hclose(self->handle);
        log_assert(ret == 0);

        flexframesync_destroy(self->fs);
        msresamp_crcf_destroy(self->resamp);
        nco_crcf_destroy(self->nco);
        firhilbf_destroy(self->fh);

        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}