#include "metrics.h"

void metrics_on_deliver(Metrics *m, const Packet *p, int now_tti, int bits_just_sent) {
    (void)bits_just_sent;
    int latency = now_tti - p->arrival_tti;
    if (latency < 0) latency = 0;
    m->sum_latency += latency;
    // delivered packets are accounted implicitly as total_packets - deadline_misses
}

void metrics_on_miss(Metrics *m, const Packet *p) {
    (void)p;
    m->deadline_misses++;
}
