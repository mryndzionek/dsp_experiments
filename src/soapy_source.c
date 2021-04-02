#include "soapy_source.h"

#include <complex.h>
#include <math.h>

#include <libdill.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include "logging.h"
#include "util.h"

struct _soapy_source_t
{
    link_t *out;
    SoapySDRDevice *sdr;
    SoapySDRStream *rxStream;
    int handle;
};

static coroutine void soapy_source_runner(soapy_source_t *self)
{
    int ret, flags;
    int read;
    size_t n;
    size_t to_read = self->out->out_bs;
    long long timeNs;
    link_msg_t msg = {
        .len = 0,
        .id = 0
    };

    complex float *buffp = malloc(to_read * sizeof(complex float));
    log_assert(buffp);
    void *buffs[] = {buffp};

    while (true)
    {
        read = SoapySDRDevice_readStream(self->sdr, self->rxStream, buffs, to_read, &flags, &timeNs, 200000);
        if (read > 0)
        {
            dlg_assertm(read <= to_read, "read = %d", read);
            LOG(DEBUG, "Sending %d samples (%p)", read, self->out->out_buf);

            n = lws_ring_insert(self->out->out_buf, buffp, read);
            log_assert(n == read);
            
            to_read -= read;
            if (to_read == 0)
            {
                to_read = self->out->out_bs;
                msg.len = to_read;
                ret = chsend(self->out->out_ch_s, &msg, sizeof(link_msg_t), -1);
                if (ret == 0)
                {
                    ret = yield();
                    log_assert(ret == 0);
                }
                else
                {
                    break;
                } 
            }
        }
        else
        {
            LOG(WARN, "No samples read");
            ret = chsend(cancel_ch, NULL, 0, -1);
            log_assert(ret == 0);
            break;
        }
    }
    ret = chdone(self->out->in_ch_s);
    log_assert(ret == 0);

    ret = hclose(self->out->in_ch_s);
    log_assert(ret == 0);
    lws_ring_destroy(self->out->in_buf);
    LOG(DEBUG, "Exiting");
}

soapy_source_t *soapy_source_create(double samplerate, double frequency,
                                    link_t *output)
{
    int ret;
    size_t length;
    char *driver_name = NULL;
    soapy_source_t *self = NULL;

    SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
    for (size_t i = 0; i < length; i++)
    {
        LOG(INFO, "Found device #%d: ", (int)i);
        for (size_t j = 0; j < results[i].size; j++)
        {
            LOG(INFO, "%s=%s, ", results[i].keys[j], results[i].vals[j]);
            if (strncmp(results[i].keys[j], "driver", sizeof("driver")) == 0)
            {
                driver_name = results[i].vals[j];
            }
        }
    }

    if (driver_name)
    {
        LOG(INFO, "Using %s device", driver_name);
        
        self = (soapy_source_t *)malloc(sizeof(soapy_source_t));
        log_assert(self);

        self->out = output;

        SoapySDRKwargs args = {};
        SoapySDRKwargs_set(&args, "driver", driver_name);
        self->sdr = SoapySDRDevice_make(&args);
        log_assert(self->sdr);
        SoapySDRKwargs_clear(&args);

        SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(self->sdr, SOAPY_SDR_RX, 0, &length);
        LOG(INFO, "Rx freq ranges: ");
        for (size_t i = 0; i < length; i++)
            LOG(INFO, "[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
        free(ranges);

        size_t num_rxch = SoapySDRDevice_getNumChannels(self->sdr, SOAPY_SDR_RX);
        LOG(INFO, "Rx num channels: %lu", num_rxch);
        log_assert(num_rxch == 1);

        ret = SoapySDRDevice_setSampleRate(self->sdr, SOAPY_SDR_RX, 0, samplerate);
        log_assert(ret == 0);
        ret = SoapySDRDevice_setFrequency(self->sdr, SOAPY_SDR_RX, 0, frequency, NULL);
        log_assert(ret == 0);
        ret = SoapySDRDevice_setupStream(self->sdr, &self->rxStream, SOAPY_SDR_RX, SOAPY_SDR_CF32, NULL, 0, NULL);
        log_assert(ret == 0);
        ret = SoapySDRDevice_activateStream(self->sdr, self->rxStream, 0, 0, 0);
        log_assert(ret == 0);
    }
    else
    {
        LOG(ERROR, "No Soapy SDR device found");
    }

    SoapySDRKwargsList_clear(results, length);
    return self;
}

void soapy_source_start(soapy_source_t *self)
{
    self->handle = go(soapy_source_runner(self));
    log_assert(self->handle >= 0);
}

void soapy_source_set_frequency(soapy_source_t *self, double frequency)
{
    int ret = SoapySDRDevice_setFrequency(self->sdr, SOAPY_SDR_RX, 0, frequency, NULL);
    log_assert(ret == 0);
}

void soapy_source_destroy(soapy_source_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        int ret;
        soapy_source_t *self = *self_p;

        ret = hclose(self->handle);
        log_assert(ret == 0);

        ret = SoapySDRDevice_deactivateStream(self->sdr, self->rxStream, 0, 0);
        log_assert(ret == 0);
        ret = SoapySDRDevice_closeStream(self->sdr, self->rxStream);
        log_assert(ret == 0);

        SoapySDRDevice_unmake(self->sdr);

        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}
