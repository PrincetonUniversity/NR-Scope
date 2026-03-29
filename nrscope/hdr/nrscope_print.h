// simple logging helpers with global log level options
#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>

extern bool g_silent;

#define NRSCOPE_PRINT(fmt, ...) do { if (!g_silent) printf(fmt, ##__VA_ARGS__); } while(0)

#define NRSCOPE_PRINT_ERROR(fmt, ...) do { printf("\e[31m" fmt "\e[0m\n", ##__VA_ARGS__); } while(0)


// Helper macros for timing code blocks

// #define TSTART(name) struct timeval name##_t0, name##_t1; gettimeofday(&name##_t0, NULL);
// #define TEND(name)   gettimeofday(&name##_t1, NULL); \
//   printf(#name ": %ld (us)\n", (name##_t1.tv_sec - name##_t0.tv_sec) * 1000000L + (name##_t1.tv_usec - name##_t0.tv_usec));
#define TSTART(name)
#define TEND(name)
