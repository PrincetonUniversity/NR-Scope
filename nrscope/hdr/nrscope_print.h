// simple logging helpers with global log level options
#pragma once
#include <stdio.h>
#include <stdbool.h>

extern bool g_silent;

#define NRSCOPE_PRINT(fmt, ...) do { if (!g_silent) printf(fmt, ##__VA_ARGS__); } while(0)
