#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <termios.h>

#include <complex.h>
#include <math.h>

#include <libdill.h>
#include <libwebsockets.h>
#include <liquid/liquid.h>

#include "logging.h"
#include "link.h"
#include "util.h"

#include "resampler.h"
#include "wbfm_demod.h"
#include "fms_demod.h"
#include "soapy_source.h"
#include "audio_sink.h"

#define SDR_SAMPLERATE (1000000UL)
#define SDR_OFFSET_FREQ_HZ (0)
#define SDR_NUM_SAMPLES (10 * 1000UL)

#define AUDIO_SAMPLERATE (48000UL)
#define DECIMATION_FACTOR (4UL)
#define SDR_RESAMPLERATE (DECIMATION_FACTOR * AUDIO_SAMPLERATE)

#define CFG_FILE_NAME ("stations.txt")

static double *frequencies;
static size_t freq_n;
static bool stereo = false;

static const char help_msg[] =
    "wbfm_demod, a simple wide band FM demodulator application\n\n"
    "Use:\twbfm_demod [-s]\n"
    "\t-s use stereo mode instead of mono\n";

static bool parse_args(int argc, char *argv[])
{
    int opt;
    bool ret = true;

    while (ret && (opt = getopt(argc, argv, "sh")) != -1)
    {
        switch (opt)
        {
        case 's':
            stereo = true;
            break;

        case 'h':
            fprintf(stderr, help_msg);
            ret = false;
            break;

        default:
            fprintf(stderr, "\n");
            fprintf(stderr, help_msg);
            ret = false;
            break;
        }
    }

    return ret;
}

static coroutine void key_press_handler(int out_ch)
{
    int ret;
    link_msg_t msg;

    static struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    while (true)
    {
        ret = fdin(STDIN_FILENO, -1);
        if (ret < 0)
        {
            break;
        }
        msg.id = getc(stdin);
        msg.len = 1;
        ret = chsend(out_ch, &msg, sizeof(link_msg_t), -1);
        if (ret < 0)
        {
            break;
        }
    }
    ret = chdone(out_ch);
    log_assert(ret == 0);

    fdclean(STDIN_FILENO);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static bool agc_handler(void *ctx, void *in_buf, const link_msg_t *in_msg,
                        void *out_buf, link_msg_t *out_msg)
{
    float complex y;
    float rssi_sum = 0.0;

    for (size_t i = 0; i < in_msg->len; i++)
    {
        agc_crcf_execute((agc_crcf)ctx, ((float complex *)in_buf)[i], &y);
        rssi_sum += agc_crcf_get_rssi((agc_crcf)ctx);
    }
    *((float *)out_buf) = rssi_sum / in_msg->len;
    out_msg->len = 1;
    out_msg->id = 0;

    return true;
}

static void coroutine timeout(int out_ch, size_t delay)
{
    msleep(now() + delay);
    chsend(out_ch, NULL, 0, -1);
}

static void scan(FILE *ofile, soapy_source_t *source, link_t *signal)
{
    int ret;
    bool detected = false;
    int tch[2], th;
    link_msg_t msg;
    float noise_floor;
    agc_crcf agc = agc_crcf_create();
    log_assert(agc);

    ret = chmake(tch);
    log_assert(ret == 0);
    log_assert(tch[0] >= 0);
    log_assert(tch[1] >= 0);

    link_t *agc_link = link_connect("agc", signal, 2,
                                    signal->out_bs, sizeof(complex float),
                                    1, sizeof(float));
    log_assert(agc_link);

    link_t *out_link = link_connect("out", agc_link, 2,
                                    agc_link->out_bs, sizeof(float),
                                    agc_link->out_bs, sizeof(float));
    log_assert(out_link);

    struct chclause clauses[] = {
        {CHRECV, out_link->in_ch_r, &msg, sizeof(link_msg_t)},
        {CHRECV, tch[1], NULL, 0}};

    int agc_handle = go(link_run(agc_link, agc, agc_handler));
    log_assert(agc_handle >= 0);
    // 20MHz between 88-108, scanning every 100kHz
    for (int i = -1; i < 201; i++)
    {
        double f = i < 0 ? 87.0e6 : 88.0e6 + (100.0e3 * i);
        float rssi, max_rssi, rssi_sum = 0.0;
        double freq;
        size_t n, rssi_n = 0;

        LOG(DEBUG, "Scanning frequency [%d] %lf", i, f);
        soapy_source_set_frequency(source, f);
        agc_crcf_reset(agc);
        th = go(timeout(tch[0], 500));

        while (true)
        {
            ret = choose(clauses, 2, -1);
            if (ret < 0)
            {
                goto exit;
            }
            else if (ret == 0)
            {
                log_assert(msg.len == 1);
                n = lws_ring_consume(out_link->in_buf, NULL, &rssi, 1);
                log_assert(n == msg.len);
                rssi_sum += rssi;
                rssi_n++;
            }
            else
            {
                ret = hclose(th);
                log_assert(ret == 0);
                break;
            }
        }
        rssi = rssi_sum / rssi_n;
        LOG(DEBUG, "RSSI = %f dBm, frequency = %lf Hz", rssi, f);
        if(i < 0)
        {
            noise_floor = rssi;
            LOG(INFO, "Noise floor is: %f dBm", noise_floor);
        } else {
            if (rssi > (noise_floor + 5.0))
            {
                if (!detected)
                {
                    max_rssi = rssi;
                    freq = f;
                    detected = true;
                }
                else
                {
                    if (rssi > max_rssi)
                    {
                        max_rssi = rssi;
                        freq = f;
                    }
                }
            }
            else
            {
                if (detected)
                {
                    detected = false;
                    LOG(INFO, "Found possible station - RSSI = %f dBm, frequency = %lf Hz", max_rssi, freq);
                    fprintf(ofile, "%f\n", freq);
                }
            }
        }
    }

exit:
    ret = hclose(agc_handle);
    log_assert(ret == 0);
    agc_crcf_destroy(agc);
}

int main(int argc, char *argv[])
{
    int ret;
    link_t *src_link = NULL;

    logging_init();

    ret = parse_args(argc, argv);
    if(!ret)
    {
        exit(EXIT_FAILURE);
    }

    src_link = link_connect("soapy_source", NULL, 0, SDR_NUM_SAMPLES, sizeof(complex float),
                            SDR_NUM_SAMPLES, sizeof(complex float));
    log_assert(src_link);
    soapy_source_t *iq_source = soapy_source_create(SDR_SAMPLERATE, 88.0e6, src_link);
    if (iq_source)
    {
        fms_demod_t *fms_demod;
        wbfm_demod_t *wbfm_demod;
        audio_sink_t *sink;
        link_t *rsmp_link = NULL;

        resampler_t *resamp = resampler_create(SDR_SAMPLERATE, SDR_RESAMPLERATE,
                                               SDR_OFFSET_FREQ_HZ, src_link);
        log_assert(resamp);
        rsmp_link = resampler_get_output(resamp);
        log_assert(rsmp_link);

        log_assert((SDR_RESAMPLERATE % AUDIO_SAMPLERATE) == 0);

        FILE *cfg;

        cfg = fopen(CFG_FILE_NAME, "r");
        if (cfg)
        {
            float f;

            LOG(INFO, "Configuration file %s found. Reading station frequencies", CFG_FILE_NAME);

            while(true)
            {
                ret = fscanf(cfg, "%f", &f);
                if(ret != 1)
                {
                    break;
                }
                freq_n++;
            }

            if (freq_n)
            {
                size_t i;

                frequencies = malloc(freq_n * sizeof(double));
                log_assert(frequencies);
                ret = fseek(cfg , 0, SEEK_SET);
                log_assert(ret == 0);

                for(i = 0; i < freq_n; i++)
                {
                    ret = fscanf(cfg, "%f", &f);
                    if(ret != 1)
                    {
                        break;
                    }
                    frequencies[i] = f;
                }
                freq_n = i;
            }

            ret = fclose(cfg);
            log_assert(ret == 0);

            LOG(INFO, "Loaded %lu frequencies from file", freq_n);
        }

        soapy_source_start(iq_source);
        
        if (freq_n == 0)
        {
            LOG(INFO, "Scanning for stations. Please wait...");
            cfg = fopen(CFG_FILE_NAME, "w");

            log_assert(cfg);
            scan(cfg, iq_source, rsmp_link);

            ret = fclose(cfg);
            log_assert(ret == 0);
        }
        else
        {
            if (stereo)
            {
                LOG(INFO, "Stereo mode");
                fms_demod = fms_demod_create(SDR_RESAMPLERATE, DECIMATION_FACTOR, rsmp_link);
                link_t *demod_link = fms_demod_get_output(fms_demod);
                sink = audio_sink_create(AUDIO_SAMPLERATE, 2, demod_link);
            }
            else
            {
                LOG(INFO, "Mono mode");
                wbfm_demod = wbfm_demod_create(SDR_RESAMPLERATE, DECIMATION_FACTOR, rsmp_link);
                link_t *demod_link = wbfm_demod_get_output(wbfm_demod);
                sink = audio_sink_create(AUDIO_SAMPLERATE, 1, demod_link);
            }

            {
                int ret;
                int key_ch[2];
                link_msg_t msg;
                size_t curr_f = 0;

                soapy_source_set_frequency(iq_source, frequencies[curr_f]);

                ret = chmake(key_ch);
                log_assert(ret == 0);
                log_assert(key_ch[0] >= 0);
                log_assert(key_ch[1] >= 0);

                int cc = install_sigint_handler();

                int kh = go(key_press_handler(key_ch[0]));
                log_assert(kh >= 0);

                struct chclause clauses[] = {
                    {CHRECV, key_ch[1], &msg, sizeof(link_msg_t)},
                    {CHRECV, cc, NULL, 0}};

                while (true)
                {
                    ret = choose(clauses, 2, -1);
                    switch (ret)
                    {
                    case 0:
                        curr_f++;

                        if (curr_f >= freq_n)
                        {
                            curr_f = 0;
                        }
                        LOG(INFO, "Setting frequency: %lf", frequencies[curr_f]);
                        soapy_source_set_frequency(iq_source, frequencies[curr_f]);
                        break;

                    default:
                        goto exit;
                        break;
                    }
                }
exit:
                clean_sigint_handler();
                ret = hclose(kh);
                log_assert(ret == 0);
                ret = hclose(key_ch[0]);
                log_assert(ret == 0);
                ret = hclose(key_ch[1]);
                log_assert(ret == 0);
            }
            LOG(INFO, "Exiting application");

            soapy_source_destroy(&iq_source);
            resampler_destroy(&resamp);
            if (stereo)
            {
                fms_demod_destroy(&fms_demod);
            }
            else
            {
                wbfm_demod_destroy(&wbfm_demod);
            }
            audio_sink_destroy(&sink);
        }

    }

    LOG(INFO, "Exiting");
    exit(EXIT_SUCCESS);
}
