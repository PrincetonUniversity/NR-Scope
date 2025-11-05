#ifndef SRSGUI_PLOT_H
#define SRSGUI_PLOT_H

#include "srsgui/srsgui.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#include "srsran/srsran.h"

#ifdef __cplusplus
extern "C" {
#endif

void push_node(cf_t *symbols, int M);

void init_plots();

#ifdef __cplusplus
}
#endif

#endif

