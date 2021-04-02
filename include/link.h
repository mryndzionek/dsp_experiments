#ifndef __LINK_H__
#define __LINK_H__

#include <stddef.h>
#include <stdbool.h>
#include <libdill.h>

typedef struct
{
    const char *name;
    int in_ch_s;
    int in_ch_r;
    struct lws_ring *in_buf;
    size_t in_sz;
    size_t in_bs;
    size_t in_nb;

    int out_ch_s;
    struct lws_ring *out_buf;
    size_t out_sz;
    size_t out_bs;

    bool async;
} link_t;

typedef struct
{
    size_t len;
    int id;
} link_msg_t;

typedef bool (*link_handler_t)(void *, void *, const link_msg_t *, void *, link_msg_t *);

link_t *link_connect(const char *name, link_t *src, size_t in_nb, size_t in_bs, size_t in_sz,
                     size_t out_bs, size_t out_sz);
coroutine void link_run(link_t *self, void *ctx, link_handler_t handler);

#endif // __LINK_H__