#ifndef SIM_H
#define SIM_H

#include "common.h"
#include "phy.h"

typedef struct {
    Config  cfg;
    int     tti;
    Metrics m;
    UE     *ues;

    // HARQ ring buffer
    HarqEvent *harq_events;
    int harq_cap, harq_head, harq_tail, harq_count;

    // Logging
    FILE *csv;       // per-UE schedule log
    FILE *evcsv;     // HARQ events log
    FILE *chcsv;     // channel log

    Phy phy;
} Sim;

void sim_init(Sim *s, const Config *cfg);
void sim_free(Sim *s);
void sim_step(Sim *s);
void sim_run(Sim *s);
void sim_print_summary(const Sim *s);

#endif // SIM_H
