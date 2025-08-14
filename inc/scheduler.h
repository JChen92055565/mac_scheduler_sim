#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

typedef struct {
    int ue_id;
    int pkt_arrival_tti;
    int pkt_deadline_tti;
    int pkt_size_bits;

    int    rb_alloc;             // RBs allocated to finish this TB now
    int    cqi_at_tx;
    double sinr_db_at_tx;
    double rb_err_prob_at_tx;
} Completion;

// bits/RB utility (legacy table). Scheduler will prefer UE.bprb_cur if available.
int bits_per_rb_for_cqi(int cqi);

// EDF scheduler
int schedule_edf(
    UE *ues, int num_ues, int rb_budget, int now_tti, Metrics *m, int *rb_used_out,
    Completion *comps, int comps_cap, int *comps_used
);

#endif // SCHEDULER_H
