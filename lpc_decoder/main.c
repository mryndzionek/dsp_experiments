#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include "fix.h"
#include "lpc.h"

static void gen_time(lpc_seq_decoder_t *dec, uint8_t hour, uint8_t minute)
{
    size_t i = 1;
    lpc_seq_t const *s[MAX_DECODER_SEQ] = {lpc_get_seq(LPC_JEST_GODZINA)};

    if (hour > 23)
    {
        return;
    }

    if (minute > 59)
    {
        return;
    }

    // fprintf(stderr, "hour: %d, minute: %d\n", hour, minute);

    s[i++] = lpc_get_seq(hour < 21 ? hour : 20);
    if (hour > 20)
    {
        s[i++] = lpc_get_seq(hour - 20);
    }

    if (minute < 21)
    {
        if (minute == 0)
        {
            s[i++] = lpc_get_seq(LPC_ZERO);
        }
        s[i++] = lpc_get_seq(minute + LPC_ZERO);
    }
    else
    {
        s[i++] = lpc_get_seq(LPC_OSIEMNASCIE + (minute / 10));
        if (minute % 10)
        {
            s[i++] = lpc_get_seq(LPC_ZERO + (minute % 10));
        }
    }

    size_t all = lpc_seq_decoder_update(dec, (const lpc_seq_t *const *)&s, i);

    {
        bool finished = false;
        size_t samples = 0;
        fix16_t y;
        int16_t buf_out[all];

        while (!finished)
        {
            uint32_t rnd = rand();
            finished = lpc_seq_decoder_exec(dec, rnd, &y);
            buf_out[samples++] = (int64_t)y * INT16_MAX / (4 * FIX_ONE);
        }

        size_t written = fwrite(buf_out, 2, all, stdout);
        assert(written == all);
        fflush(stdout);
    }
}

int main(int argc, char *argv[])
{
    lpc_filter_t *f = lpc_filter_new();
    assert(f);

    lpc_seq_decoder_t *dec = lpc_seq_decoder_new();
    assert(dec);

    for (uint8_t h = 0; h < 24; h++)
    {
        for (uint8_t m = 0; m < 60; m++)
        {
            gen_time(dec, h, m);
        }
    }

    exit(EXIT_SUCCESS);
}
