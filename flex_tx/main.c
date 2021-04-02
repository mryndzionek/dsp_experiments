#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <complex.h>

#include "logging.h"
#include "util.h"

#include "flex_encoder.h"
#include "audio_sink.h"

#define AUDIO_SAMPLERATE (48000UL)

#define TEST_MESSAGE "TEST MESSAGE: 123456789"
#define TEST_MESSAGE_SIZE (sizeof(TEST_MESSAGE) - 1)

int main(int argc, char *argv[])
{
    size_t n;
    int ret;
    link_msg_t msg = {
        .len = 0,
        .id = 0};

    logging_init();

    link_t *src_link = link_connect("keyboard_source", NULL, 0, 0, sizeof(char),
                                    1024, sizeof(char));
    log_assert(src_link);
    flex_encoder_t *source = flex_encoder_create(src_link);
    audio_sink_t *sink = audio_sink_create(AUDIO_SAMPLERATE, 1, flex_encoder_get_output(source));

    int cc = install_sigint_handler();
    log_assert(cc >= 0);

    while (true)
    {
        n = lws_ring_insert(src_link->out_buf, TEST_MESSAGE, TEST_MESSAGE_SIZE);
        log_assert(n == TEST_MESSAGE_SIZE);
        msg.len = TEST_MESSAGE_SIZE;


        ret = chsend(src_link->out_ch_s, &msg, sizeof(link_msg_t), -1);
        if (ret != 0)
        {
            break;
        }

        ret = chrecv(cc, &msg, sizeof(link_msg_t), now() + 1000);
        if(ret == 0)
        {
            log_assert(msg.id == -1);
            break;
        }
        log_assert(errno == ETIMEDOUT);
    }

    clean_sigint_handler();
    flex_encoder_destroy(&source);
    audio_sink_destroy(&sink);

    LOG(INFO, "Exiting");
    exit(EXIT_SUCCESS);
}