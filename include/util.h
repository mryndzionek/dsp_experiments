#ifndef __UTIL_H__
#define __UTIL_H__

extern int cancel_ch;

int install_sigint_handler(void);
void clean_sigint_handler(void);

#endif // __UTIL_H__