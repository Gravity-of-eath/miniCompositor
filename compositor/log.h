#ifndef MC_LOG_H
#define MC_LOG_H

#include <stdio.h>
#include <time.h>

extern int g_log_level;  /* 0=err 1=warn 2=info 3=debug */

#define LOG_AT(lvl, tag, ...) do { \
    if (g_log_level >= (lvl)) { \
        struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts); \
        fprintf(stderr, "[%ld.%03ld][" tag "] ", \
            (long)_ts.tv_sec, (long)(_ts.tv_nsec / 1000000)); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } \
} while (0)

#define LOG_E(...) LOG_AT(0, "E", __VA_ARGS__)
#define LOG_W(...) LOG_AT(1, "W", __VA_ARGS__)
#define LOG_I(...) LOG_AT(2, "I", __VA_ARGS__)
#define LOG_D(...) LOG_AT(3, "D", __VA_ARGS__)

#endif
