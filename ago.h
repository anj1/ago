#ifndef AGO_H
#define AGO_H

int alib_thread_init(int max_conc);
int alib_go(void (*func)(void *), void *arg);
int alib_thread_wait();
int alib_thread_end();

#endif	/* AGO_H */
