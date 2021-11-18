#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

#include <complex.h>
#include <math.h>

#include <liquid/liquid.h>
#include <sndfile.h>
#include <png.h>

#include "logging.h"
#include "mel_spectrum.h"
#include "tflite_runner.h"

#define FRAME_LEN (1024UL)
#define FRAME_STEP (256UL)
#define FRAME_END (FRAME_LEN - FRAME_STEP)
#define SAMPLE_RATE (16000UL)
#define NUM_SAMPLES (1 * SAMPLE_RATE)
#define OUTPUT_BIN_SIZE (80UL)
#define NUM_OUTPUT_BINS (((NUM_SAMPLES - FRAME_LEN) / FRAME_STEP) + 1)
#define OUTPUT_SIZE (NUM_OUTPUT_BINS * OUTPUT_BIN_SIZE)

#define MODEL_FILE "../models/17_keywords.tflite"

typedef struct
{
    mel_spectrum_t *mel;
    float input[FRAME_LEN];
    float output[OUTPUT_SIZE];
    size_t n;
} ctx_t;

static void normalize(float *data, size_t size)
{
    size_t i;
    float min = data[0];
    float max = data[0];

    for (i = 1; i < OUTPUT_SIZE; i++)
    {
        if (data[i] < min)
        {
            min = data[i];
        }

        if (data[i] > max)
        {
            max = data[i];
        }
    }

    max = max - min;

    for (i = 1; i < OUTPUT_SIZE; i++)
    {
        data[i] = roundf(255.0 * ((data[i] - min) / max));
    }
}

int main(int argc, char *argv[])
{
    logging_init();

    if (argc < 2)
    {
        LOG(ERROR, "Please provide WAV file to process");
        exit(EXIT_FAILURE);
    }

    char *wav_file_name = argv[1];

    ctx_t ctx;
    size_t i;
    void *dest;
    SF_INFO sfinfo;
    sf_count_t read;

    ctx.mel = mel_spectrum_create(FRAME_LEN, OUTPUT_BIN_SIZE, NUM_SAMPLES,
                                  20.0, 7600);
    log_assert(ctx.mel);

    SNDFILE *file = sf_open(wav_file_name, SFM_READ, &sfinfo);
    log_assert(file);
    log_assert(sfinfo.channels == 1);
    log_assert(sfinfo.samplerate == SAMPLE_RATE);
    LOG(INFO, "WAV file length: %ld", sfinfo.frames);

    read = sf_read_float(file, ctx.input, FRAME_LEN);
    log_assert(read == FRAME_LEN);

    mel_spectrum_process(ctx.mel, ctx.input, ctx.output);
    ctx.n = 1;

    dest = memmove(ctx.input, &ctx.input[FRAME_STEP], FRAME_END * sizeof(float));
    log_assert(dest == ctx.input);

    while ((read = sf_read_float(file, &ctx.input[FRAME_END], FRAME_STEP)) && (ctx.n < NUM_OUTPUT_BINS))
    {
        log_assert(read <= FRAME_STEP);
        mel_spectrum_process(ctx.mel, ctx.input, &ctx.output[ctx.n * OUTPUT_BIN_SIZE]);
        ctx.n++;

        dest = memmove(ctx.input, &ctx.input[FRAME_STEP], FRAME_END * sizeof(float));
        log_assert(dest == ctx.input);
        for (i = 0; i < FRAME_STEP; i++)
        {
            ctx.input[FRAME_END + i] = 0.001 * rand() / RAND_MAX;
        }
    }

    if (ctx.n < NUM_OUTPUT_BINS)
    {
        LOG(WARN, "File too short. Padding with noise");
    }

    while (ctx.n < NUM_OUTPUT_BINS)
    {
        for (i = 0; i < FRAME_STEP; i++)
        {
            ctx.input[FRAME_END + i] = 0.001 * rand() / RAND_MAX;
        }
        mel_spectrum_process(ctx.mel, ctx.input, &ctx.output[ctx.n * OUTPUT_BIN_SIZE]);

        dest = memmove(ctx.input, &ctx.input[FRAME_STEP], FRAME_END * sizeof(float));
        log_assert(dest == ctx.input);
        dest = memset(&ctx.input[FRAME_END], 0, FRAME_STEP * sizeof(float));
        log_assert(dest == &ctx.input[FRAME_END]);
        ctx.n++;
    }

    log_assert(ctx.n == NUM_OUTPUT_BINS);

    tflite_runner_t *tfr = tflite_runner_create_from_file(MODEL_FILE);
    float score;
    int id = tflite_runner_run(tfr, ctx.output, OUTPUT_SIZE, &score);
    if (id >= 0)
    {
        LOG(INFO, "Predicted keyword: %s (%f)", tflite_get_label(id), score);
    }
    else
    {
        LOG(ERROR, "Failed to predict keyword");
    }
    tflite_runner_destroy(&tfr);

    normalize(ctx.output, OUTPUT_SIZE);
    {
        png_image image;
        uint8_t buff[OUTPUT_SIZE];

        for(i = 0; i < OUTPUT_SIZE; i++)
        {
            buff[i] = (uint8_t)ctx.output[i];
        }

        memset(&image, 0, (sizeof image));
        image.version = PNG_IMAGE_VERSION;
        image.width = OUTPUT_BIN_SIZE;
        image.height = NUM_OUTPUT_BINS;
        image.format = PNG_FORMAT_GRAY;

        char *cp = strstr(wav_file_name, ".wav");
        if(cp)
        {
            strncpy(cp, ".png", 5);
            int s = png_image_write_to_file(&image, wav_file_name, 0, buff, 0, NULL);
            log_assert(s);
        }
    }

    sf_close(file);
    mel_spectrum_destroy(&ctx.mel);

    LOG(INFO, "Exiting");
    exit(EXIT_SUCCESS);
}
