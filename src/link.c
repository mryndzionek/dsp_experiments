#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <libwebsockets.h>

#include "logging.h"

link_t *link_connect(const char *name, link_t *src, size_t in_nb,
                     size_t in_bs, size_t in_sz,
                     size_t out_bs, size_t out_sz)
{
    int ret;
    int in_ch[2];

    link_t *self = (link_t *)malloc(sizeof(link_t));
    log_assert(self);
    self->name = name;
    self->async = false;

    self->in_sz = in_sz;
    self->in_bs = in_bs;
    self->in_nb = in_nb;
    self->out_sz = out_sz;
    self->out_bs = out_bs;

    ret = chmake(in_ch);
    log_assert(ret == 0);
    log_assert(in_ch[0] >= 0);
    log_assert(in_ch[1] >= 0);

    self->in_ch_s = in_ch[0];
    self->in_ch_r = in_ch[1];

    self->in_buf = lws_ring_create(self->in_sz, 1 + (self->in_nb * self->in_bs), NULL);
    log_assert(self->in_buf);
    LOG(DEBUG, "%s -> input buffer: %lu * %lu * %lu = %lu", self->name,
        self->in_nb, self->in_bs, self->in_sz,
        self->in_sz * self->in_nb * self->in_bs);

    log_assert(lws_ring_get_count_free_elements(self->in_buf) == self->in_nb * self->in_bs);

    if (src)
    {
        log_assert(src->out_sz == self->in_sz);
        src->out_ch_s = self->in_ch_s;
        src->out_buf = self->in_buf;
    }
    else
    {
        self->out_ch_s = -1;
        self->out_buf = NULL;
    }

    if (src)
    {
        LOG(DEBUG, "Links '(%s, %s)' connected", src->name, self->name);
    }
    else
    {
        LOG(DEBUG, "Link '%s' created", self->name);
    }

    return self;
}

coroutine void link_run(link_t *self, void *ctx, link_handler_t handler)
{
    int ret;
    size_t n, read = 0;
    link_msg_t in_msg, out_msg;

    dlg_assertm(self->in_buf &&
                    (self->in_ch_r >= 0) &&
                    (self->in_ch_s >= 0),
                "Initial links cannot be run !!!");

    uint8_t *out_p = malloc(self->out_sz * self->out_bs);
    log_assert(out_p);

    uint8_t *in_p = malloc(self->in_bs * self->in_sz);
    log_assert(in_p);

    LOG(DEBUG, "Running link '%s'", self->name);

    while (true)
    {
        ret = chrecv(self->in_ch_r, &in_msg, sizeof(link_msg_t), -1);
        if (ret != 0)
        {
            break;
        }
        LOG(DEBUG, "Link '%s' (%p) received %lu elements with id %d", self->name, self->in_buf, in_msg.len, in_msg.id);
        read += in_msg.len;

        while (read >= (self->async ? in_msg.len : self->in_bs))
        {
            if(!self->async){
                in_msg.len = self->in_bs;
            }
            n = lws_ring_consume(self->in_buf, NULL, in_p, in_msg.len);
            log_assert(n == in_msg.len);

            bool finished = false;

            while (!finished)
            {
                out_msg.len = 0;
                finished = handler(ctx, in_p, &in_msg, out_p, &out_msg);
                log_assert((out_msg.len * self->out_sz) <= (self->out_bs * self->out_sz));
                if (out_msg.len)
                {
                    LOG(DEBUG, "Sending out %lu elements with id %d from link '%s'", out_msg.len, out_msg.id, self->name);
                    while (true)
                    {
                        n = lws_ring_get_count_free_elements(self->out_buf);
                        if (n >= out_msg.len)
                        {
                            break;
                        }
                        LOG(WARN, "Cannot write to output");
                        ret = yield();
                        log_assert(ret == 0);
                    }
                    n = lws_ring_insert(self->out_buf, out_p, out_msg.len);
                    log_assert(n == out_msg.len);
                    ret = chsend(self->out_ch_s, &out_msg, sizeof(link_msg_t), -1);
                    if (ret != 0)
                    {
                        goto exit;
                    }
                }
            }
            read -= in_msg.len;
        }
    }

exit:
    LOG(DEBUG, "Stopping link '%s'", self->name);

    ret = chdone(self->in_ch_s);
    log_assert(ret == 0);

    ret = hclose(self->in_ch_s);
    log_assert(ret == 0);
    lws_ring_destroy(self->in_buf);
    free(in_p);
    free(out_p);

    LOG(DEBUG, "Exiting link '%s'", self->name);
}