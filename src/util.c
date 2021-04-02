#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <libdill.h>

#include "link.h"
#include "logging.h"

int cancel_ch;

static int signal_pipe[2];
static int sig_ch[2];
static int handle;

static void intHandler(int signo)
{
    LOG(INFO, "Caught signal %d", signo);
    char b = signo;
    ssize_t sz = write(signal_pipe[1], &b, 1);
    assert(sz == 1);
}

static coroutine void wait_for_sigint(void)
{
    link_msg_t msg = {
        .id = -1,
        .len = 0};
    int ret = fdin(signal_pipe[0], -1);
    if (ret == 0)
    {
        char signo;
        ssize_t sz = read(signal_pipe[0], &signo, 1);
        log_assert(sz == 1);
        log_assert(signo == SIGINT);
        ret = chsend(cancel_ch, &msg, sizeof(link_msg_t), -1);
    }
    LOG(DEBUG, "Exiting");
}

int install_sigint_handler(void)
{
    int ret = chmake(sig_ch);
    log_assert(ret == 0);
    log_assert(sig_ch[0] >= 0);
    log_assert(sig_ch[1] >= 0);

    cancel_ch = sig_ch[0];

    ret = pipe(signal_pipe);
    log_assert(ret == 0);
    signal(SIGINT, intHandler);
    handle = go(wait_for_sigint());
    log_assert(handle >= 0);

    return sig_ch[1];
}

void clean_sigint_handler(void)
{
    fdclean(signal_pipe[0]);
    int ret = close(signal_pipe[0]);
    log_assert(ret == 0);
    ret = close(signal_pipe[1]);
    log_assert(ret == 0);
    ret = hclose(handle);
    log_assert(ret == 0);
}