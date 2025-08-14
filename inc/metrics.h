#ifndef METRICS_H
#define METRICS_H

#include "common.h"

void metrics_on_deliver(Metrics *m, const Packet *p, int now_tti, int bits_just_sent);
void metrics_on_miss(Metrics *m, const Packet *p);

#endif
