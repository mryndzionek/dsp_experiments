#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include <complex.h>
#include <math.h>

#include <liquid/liquid.h>

#include "logging.h"

#include "audio_source.h"
#include "mel_spectrum.h"
#include "tflite_runner.h"
#include "17_keywords.h"

#define AUDIO_SAMPLERATE (16000UL)
#define FRAME_LEN (1024UL)
#define FRAME_STEP (256UL)
#define FRAME_END (FRAME_LEN - FRAME_STEP)
#define SLICE_SIZE (80UL)
#define SLICES (60UL)
#define SLICE_STEP (SLICES / 3)
#define OUTPUT_SIZE (SLICES * SLICE_SIZE)
#define DETECTION_THRESHOLD (10.0) // depends on audio quality

static coroutine void tf_sink(link_t *input)
{
    link_msg_t msg;
    int ret;
    void *dest;
    size_t n, m = 0, read = 0;
    float in_buf[FRAME_LEN];
    float out_buf[OUTPUT_SIZE];
    float score;
    bool detecting = false;
    float last_score;
    float last_id;

    memset(in_buf, 0, FRAME_LEN * sizeof(float));

    link_t *output = link_connect("tf_sink", input, 4,
                                  input->out_bs, sizeof(float),
                                  FRAME_LEN, sizeof(int));
    log_assert(output);

    mel_spectrum_t *mel = mel_spectrum_create(FRAME_LEN, SLICE_SIZE,
                                              AUDIO_SAMPLERATE, 20.0, 7600.0);
    log_assert(mel);

    tflite_runner_t *tfr = tflite_runner_create_from_mem(models_17_keywords_tflite, sizeof(models_17_keywords_tflite));
    log_assert(tfr);

    while (true)
    {
        ret = chrecv(output->in_ch_r, &msg, sizeof(link_msg_t), -1);
        if (ret != 0)
        {
            break;
        }
        read += msg.len;

        while(read >= FRAME_STEP)
        {
            dest = memmove(in_buf, &in_buf[FRAME_STEP], FRAME_END * sizeof(float));
            log_assert(dest == in_buf);

            n = lws_ring_consume(output->in_buf, NULL, &in_buf[FRAME_END], FRAME_STEP);
            log_assert(n == FRAME_STEP);
            read -= FRAME_STEP;

            mel_spectrum_process(mel, in_buf, &out_buf[(SLICES - SLICE_STEP + m) * SLICE_SIZE]);
            m++;
            if(m == SLICE_STEP)
            {
                m = 0;
                int id = tflite_runner_run(tfr, out_buf, OUTPUT_SIZE, &score);
                if ((id >= 0) && (score > DETECTION_THRESHOLD))
                {
                    LOG(DEBUG, "Predicted keyword: %s (score: %f)",
                        tflite_get_label(id), score);
                    if(!detecting)
                    {
                        last_score = score;
                        last_id = id;
                        detecting = true;
                    } else {
                        if(score > last_score)
                        {
                            last_score = score;
                            last_id = id;
                        }
                    }   
                }
                else
                {
                    if(detecting)
                    {
                        LOG(INFO, "Predicted keyword: %s (score: %f)",
                            tflite_get_label(last_id), last_score);
                        detecting = false;
                    }
                }
                dest = memmove(out_buf, &out_buf[SLICE_STEP * SLICE_SIZE],
                               (SLICES - SLICE_STEP) * SLICE_SIZE * sizeof(float));
                log_assert(dest == out_buf);
            }
        }

        LOG(DEBUG, "Received %lu samples (id = %d)", msg.len, msg.id);
        
    }

    tflite_runner_destroy(&tfr);
    mel_spectrum_destroy(&mel);

    ret = chdone(output->in_ch_s);
    log_assert(ret == 0);

    ret = hclose(output->in_ch_s);
    log_assert(ret == 0);
    lws_ring_destroy(output->in_buf);
    LOG(DEBUG, "Exiting");
}

int main(int argc, char *argv[])
{
    int ret;

    logging_init();
    audio_source_t *source = audio_source_create(AUDIO_SAMPLERATE);
    int h = go(tf_sink(audio_source_get_output(source)));

    audio_source_start(source);

    msleep(-1);

    audio_source_destroy(&source);
    ret = hclose(h);
    log_assert(ret == 0);

    LOG(INFO, "Exiting");
    exit(EXIT_SUCCESS);
}
