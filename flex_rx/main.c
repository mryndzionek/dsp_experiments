#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <complex.h>

#include "logging.h"
#include "util.h"

#include "audio_source.h"
#include "flex_decoder.h"

#define AUDIO_SAMPLERATE (48000UL)

static void coroutine out_read(link_t *output, size_t s)
{
    int ret;
    link_msg_t msg;

    uint8_t payload[s];

    while (true)
    {
        ret = chrecv(output->in_ch_r, &msg, sizeof(msg), -1);
        if ((ret != 0) || (msg.id == -1))
        {
            break;
        }

        log_assert(msg.len <= sizeof(payload));
        lws_ring_consume(output->in_buf, NULL, payload, msg.len);

        fprintf(stderr, "Received flex frame:\n\t");
        for (size_t i = 0; i < msg.len; i++)
        {
            fprintf(stderr, " 0x%02x", payload[i]);
            if ((((i + 1) % 16) == 0))
            {
                fprintf(stderr, "\n\t");
            }
        }
        fprintf(stderr, "\n");
    }

    LOG(INFO, "Exiting");
}

int main(int argc, char *argv[])
{
    link_msg_t msg;

    logging_init();

    audio_source_t *source = audio_source_create(AUDIO_SAMPLERATE);
    flex_decoder_t *flex = flex_decoder_create(audio_source_get_output(source));
    link_t *input = flex_decoder_get_output(flex);
    link_t *output = link_connect("print", input, 30,
                          input->out_bs, sizeof(char),
                          input->out_bs, sizeof(char));

    int cc = install_sigint_handler();

    audio_source_start(source);

    int hc = go(out_read(output, input->out_bs));
    log_assert(hc >= 0);

    int ret = chrecv(cc, &msg, sizeof(link_msg_t), -1);
    log_assert(ret == 0);

    ret = hclose(hc);
    log_assert(ret == 0);
    
    clean_sigint_handler();
    audio_source_destroy(&source);
    flex_decoder_destroy(&flex);

    LOG(INFO, "Exiting");
    exit(EXIT_SUCCESS);
}