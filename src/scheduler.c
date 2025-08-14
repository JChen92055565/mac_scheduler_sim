#include "scheduler.h"
#include "metrics.h"

// Keep legacy mapping available; scheduler prefers UE.bprb_cur when set
int bits_per_rb_for_cqi(int cqi) {
    static const int table[16] = {
        0,   48,   72,   96,  120,  144,  192,  240,
      288,  336,  408,  480,  552,  648,  744,  840
    };
    if (cqi < 1) cqi = 1;
    if (cqi > 15) cqi = 15;
    return table[cqi];
}

static bool ue_has_data(const UE *u) { return u->q_count > 0; }

// Pick UE with earliest deadline (head-of-line) among non-empty queues
static int pick_earliest_deadline(UE *ues, int num_ues, int now_tti) {
    (void)now_tti; // not used yet
    int best = -1;
    int best_deadline = 0;
    for (int i = 0; i < num_ues; ++i) {
        UE *u = &ues[i];
        if (!ue_has_data(u)) continue;
        Packet *p = &u->q[u->q_head];
        int d = p->deadline_tti;
        if (best < 0 || d < best_deadline) {
            best = i;
            best_deadline = d;
        }
    }
    return best;
}

int schedule_edf(
    UE *ues, int num_ues, int rb_budget, int now_tti, Metrics *m, int *rb_used_out,
    Completion *comps, int comps_cap, int *comps_used
) {
    (void)m;
    int bits_sent_total = 0;
    int rb_used = 0;
    *comps_used = 0;

    while (rb_budget > 0) {
        int idx = pick_earliest_deadline(ues, num_ues, now_tti);
        if (idx < 0) break;

        UE *u = &ues[idx];
        Packet *p = &u->q[u->q_head];

        // Prefer PHY-provided bprb_cur; fallback to legacy mapping
        int bprb = (u->bprb_cur > 0) ? u->bprb_cur : bits_per_rb_for_cqi(u->cqi);
        if (bprb <= 0) { rb_budget--; rb_used++; continue; }

        int rb_needed = (p->bits + bprb - 1) / bprb;
        if (rb_needed <= 0) rb_needed = 1;
        int rb_alloc = rb_needed <= rb_budget ? rb_needed : rb_budget;
        int bits_this = rb_alloc * bprb;

        u->dbg_was_scheduled = 1;
        u->dbg_tx_bits_this_tti += bits_this;

        p->bits -= bits_this;
        u->bits_sent_total += bits_this;

        if (p->bits <= 0) {
            int pkt_size_bits = bits_this + (p->bits < 0 ? p->bits : 0);
            if (pkt_size_bits < 0) pkt_size_bits = 0;

            // Record completion for HARQ (with TX-time PHY context)
            if (*comps_used < comps_cap) {
                Completion *c = &comps[*comps_used];
                c->ue_id = u->id;
                c->pkt_arrival_tti  = p->arrival_tti;
                c->pkt_deadline_tti = p->deadline_tti;
                c->pkt_size_bits    = pkt_size_bits;

                c->rb_alloc         = rb_alloc;
                c->cqi_at_tx        = u->cqi;
                c->sinr_db_at_tx    = u->sinr_db_cur;
                c->rb_err_prob_at_tx= u->rb_err_prob_cur;

                (*comps_used)++;
            }

            // Pop finished packet
            u->q_head = (u->q_head + 1) % MAX_QUEUE;
            u->q_count--;
        }

        bits_sent_total += bits_this;
        rb_budget -= rb_alloc;
        rb_used   += rb_alloc;
    }

    if (rb_used_out) *rb_used_out = rb_used;
    return bits_sent_total;
}
