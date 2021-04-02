#include "audio_source.h"

#include <stdio.h>
#include <rtaudio/rtaudio_c.h>
#include <libdill.h>

#include "logging.h"

#define SCALE (0.1)
#define BLOCK_SIZE (1000)
#define NUM_CHANNELS (1)

struct _audio_source_t
{
    rtaudio_t adc;
    unsigned int bufferFrames;
    unsigned int num_channels;
    link_t *out;
    int handle;
    int pipe[2];
};

static int audio_cb(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
                    double stream_time, rtaudio_stream_status_t status, void *data)
{
    int ret;
    uint8_t v;
    size_t n;
    float *buffer = (float *)inputBuffer;
    audio_source_t *self = (audio_source_t *)data;
    (void)outputBuffer;

    if (status)
        LOG(WARN, "Stream underflow detected!");

    log_assert((self->num_channels * nBufferFrames) == self->out->out_bs);

    LOG(DEBUG, "Received audio input samples");
    n = lws_ring_insert(self->out->out_buf, buffer, self->out->out_bs);
    log_assert(n == self->out->out_bs);
    ret = write(self->pipe[1], &v, 1);
    log_assert(ret == 1);

    return 0;
}

static void error_cb(rtaudio_error_t err, const char *msg)
{
    LOG(WARN, "Error type: %d message: %s", err, msg);
}

static void coroutine audio_source_runner(audio_source_t *self)
{
    int ret;
    uint8_t v;

    link_msg_t msg = {
        .len = self->out->out_bs,
        .id = 0};

    while (true)
    {
        ret = fdin(self->pipe[0], -1);
        if(ret != 0)
        {
            break;
        }

        ret = read(self->pipe[0], &v, 1);
        log_assert(ret == 1);

        LOG(DEBUG, "Sending out audio samples");

        ret = chsend(self->out->out_ch_s, &msg, sizeof(link_msg_t), -1);
        if (ret == 0)
        {
            ret = yield();
            log_assert(ret == 0);
        }
        else
        {
            log_assert((errno == ETIMEDOUT) || (errno == ECANCELED));
            break;
        }
    }

    ret = chdone(self->out->in_ch_s);
    log_assert(ret == 0);

    ret = hclose(self->out->in_ch_s);
    log_assert(ret == 0);
    LOG(DEBUG, "Exiting");
}

audio_source_t *audio_source_create(unsigned int samplerate)
{    
    int ret;
    const rtaudio_api_t *api = rtaudio_compiled_api();
    rtaudio_t adc = rtaudio_create(*api);

    int n = rtaudio_device_count(adc);
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

    audio_source_t *self = (audio_source_t *)malloc(sizeof(audio_source_t));
    log_assert(self);

    self->adc = adc;
    rtaudio_show_warnings(self->adc, true);

    rtaudio_stream_parameters_t i_params = {
        .device_id = rtaudio_get_default_input_device(self->adc),
        .first_channel = 0,
        .num_channels = NUM_CHANNELS
    };

    self->bufferFrames = BLOCK_SIZE;
    self->num_channels = NUM_CHANNELS;

    self->out = link_connect("audio_source", NULL, 0,
                             0, sizeof(float),
                             BLOCK_SIZE, sizeof(float));
    log_assert(self->out);

    // the pipe is just used as a non-blocking (in libdill) semaphore
    ret = pipe(self->pipe);
    log_assert(ret == 0);

    rtaudio_error_t err = rtaudio_open_stream(self->adc, NULL, &i_params, RTAUDIO_FORMAT_FLOAT32,
                                              samplerate, &self->bufferFrames, &audio_cb,
                                              (void *)self, NULL,
                                              &error_cb);
    log_assert(err == 0);

    return self;
}

void audio_source_start(audio_source_t *self)
{
    self->handle = go(audio_source_runner(self));
    log_assert(self->handle >= 0);

    rtaudio_error_t err = rtaudio_start_stream(self->adc);
    log_assert(err == 0);
}

link_t *audio_source_get_output(audio_source_t *self)
{
    return self->out;
}

void audio_source_destroy(audio_source_t **self_p)
{
    log_assert(self_p);

    if (*self_p)
    {
        int ret;
        audio_source_t *self = *self_p;

        ret = hclose(self->handle);
        log_assert(ret == 0);

        rtaudio_error_t err = rtaudio_stop_stream(self->adc);
        log_assert(err == 0);
        if (rtaudio_is_stream_open(self->adc))
        {
            rtaudio_close_stream(self->adc);
        }

        lws_ring_destroy(self->out->in_buf);

        fdclean(self->pipe[0]);
        ret = close(self->pipe[0]);
        log_assert(ret == 0);
        ret = close(self->pipe[1]);
        log_assert(ret == 0);

        free(self);
        *self_p = NULL;
    }
}
