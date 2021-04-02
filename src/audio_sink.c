#include "audio_sink.h"

#include <stdio.h>
#include <rtaudio/rtaudio_c.h>
#include <libdill.h>

#include "logging.h"

#define SCALE (0.1)

struct _audio_sink_t
{
    rtaudio_t dac;
    rtaudio_stream_options_t options;
    unsigned int bufferFrames;
    unsigned int num_channels;
    link_t *in;
    int handle;
    size_t avail;
};

static int audio_cb(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
                    double stream_time, rtaudio_stream_status_t status, void *data)
{
    size_t n;
    float *buffer = (float *)outputBuffer;
    audio_sink_t *self = (audio_sink_t *)data;
    (void)inputBuffer;

    if (status)
        LOG(WARN, "Stream underflow detected!");

    if (self->avail >= self->num_channels * nBufferFrames)
    {
        n = lws_ring_consume(self->in->in_buf, NULL, buffer, self->num_channels * nBufferFrames);
        log_assert(n == self->num_channels * nBufferFrames);
        for (size_t i = 0; i < self->num_channels * nBufferFrames; i++)
        {
            buffer[i] *= SCALE;
        }
        self->avail -= self->num_channels * nBufferFrames;
    } else {
        for (size_t i = 0; i < self->num_channels * nBufferFrames; i++)
        {
            buffer[i] = 0.0;
        }
    }

    //LOG(DEBUG, "Audio samples left: %ld", self->avail);

    return 0;
}

static void error_cb(rtaudio_error_t err, const char *msg)
{
    LOG(WARN, "Error type: %d message: %s", err, msg);
}

static coroutine void audio_sink_runner(audio_sink_t *self)
{
    link_msg_t msg;
    int ret;

    while (true)
    {
        ret = chrecv(self->in->in_ch_r, &msg, sizeof(link_msg_t), -1);
        if (ret != 0)
        {
            break;
        }
        LOG(DEBUG, "Received %lu samples (id = %d)", msg.len, msg.id);
        self->avail += msg.len;
    }

    ret = chdone(self->in->in_ch_s);
    log_assert(ret == 0);

    ret = hclose(self->in->in_ch_s);
    log_assert(ret == 0);
    lws_ring_destroy(self->in->in_buf);
    LOG(DEBUG, "Exiting");
}

audio_sink_t *audio_sink_create(unsigned int samplerate, unsigned int num_channels,
                                link_t *input)
{
    log_assert(num_channels < 3 && num_channels > 0);
    
    const rtaudio_api_t *api = rtaudio_compiled_api();
    rtaudio_t dac = rtaudio_create(*api);

    int n = rtaudio_device_count(dac);
    if (n == 0)
    {
        return NULL;
    }
    else if (n == 1)
    {
        LOG(INFO, "There is %d audio device available", n);
    }
    else
    {
        LOG(INFO, "There are %d audio devices available", n);
    }
    LOG(INFO, "Using default audio device");

    audio_sink_t *self = (audio_sink_t *)malloc(sizeof(audio_sink_t));
    log_assert(self);

    self->dac = dac;
    rtaudio_show_warnings(self->dac, true);

    rtaudio_stream_parameters_t o_params = {
        .device_id = rtaudio_get_default_output_device(self->dac),
        .first_channel = 0,
        .num_channels = num_channels
    };

    self->options.flags = RTAUDIO_FLAGS_HOG_DEVICE;
    self->options.flags |= RTAUDIO_FLAGS_MINIMIZE_LATENCY;
    self->bufferFrames = input->out_bs;
    self->num_channels = num_channels;

    self->in = link_connect("audio_sink", input, 50,
                            input->out_bs, sizeof(float),
                            input->out_bs, sizeof(float));
    log_assert(self->in);
    self->avail = 0;

    rtaudio_error_t err = rtaudio_open_stream(self->dac, &o_params, NULL, RTAUDIO_FORMAT_FLOAT32,
                                              samplerate, &self->bufferFrames, &audio_cb,
                                              (void *)self, &self->options,
                                              &error_cb);
    log_assert(err == 0);

    err = rtaudio_start_stream(self->dac);
    log_assert(err == 0);

    self->handle = go(audio_sink_runner(self));
    log_assert(self->handle >= 0);

    return self;
}

void audio_sink_destroy(audio_sink_t **self_p)
{
    log_assert(self_p);
    if (*self_p)
    {
        int ret;
        audio_sink_t *self = *self_p;

        ret = hclose(self->handle);
        log_assert(ret == 0);

        rtaudio_error_t err = rtaudio_stop_stream(self->dac);
        log_assert(err == 0);
        if (rtaudio_is_stream_open(self->dac))
        {
            rtaudio_close_stream(self->dac);
        }
        free(self);
        *self_p = NULL;
    }
}
